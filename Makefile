# dawatch build — targets per engine.README / main.README §11
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
LDLIBS  := -lm

ENGINE  := engine/engine_demo
CLIENT  := honeyjars/canary_client
DEMO    := honeyjars/canary_demo
SMOKE_LOG := /tmp/dawatch_smoke.log

all: $(ENGINE) $(CLIENT) $(DEMO)

$(ENGINE): engine/engine.c engine/ringbuffer.c engine/contracts.h engine/ringbuffer.h
	$(CC) $(CFLAGS) -o $@ engine/engine.c engine/ringbuffer.c $(LDLIBS)

$(CLIENT): honeyjars/canary_client.c engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_client.c

$(DEMO): honeyjars/canary_demo.c honeyjars/canary_contact.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_demo.c engine/ringbuffer.c $(LDLIBS)

# engine demo (30s) in background, client connects mid-run, then grep the log
# for each deterministically fireable trigger line. The event-trigger line
# ([BASELINE] event=1) is NOT smoke-checked yet: canary self-dilution pins
# fused z below k=3.0 until the canary split lands (engine.README matrix).
smoke: $(ENGINE) $(CLIENT)
	@rm -f $(SMOKE_LOG); \
	$(ENGINE) > $(SMOKE_LOG) 2>&1 & \
	eng=$$!; \
	ok=0; \
	sleep 1; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		$(CLIENT) >/dev/null 2>&1 && { ok=1; break; } || sleep 0.5; \
	done; \
	wait $$eng || true; \
	status=0; \
	grep -q '\[BASELINE\]' $(SMOKE_LOG) || { echo "[smoke] FAIL: no baseline ticks"; status=1; }; \
	grep -q '\[RANDOM\]' $(SMOKE_LOG) || { echo "[smoke] FAIL: no random trigger fired"; status=1; }; \
	grep -q 'received contact lane=4' $(SMOKE_LOG) || { echo "[smoke] FAIL: canary client contact not ingested"; status=1; }; \
	grep -q '\[CHECKIN\] Demo completed' $(SMOKE_LOG) || { echo "[smoke] FAIL: engine demo did not finish cleanly"; status=1; }; \
	test $$ok -eq 1 || { echo "[smoke] FAIL: canary_client never connected"; status=1; }; \
	if [ $$status -eq 0 ]; then \
		echo "[smoke] PASS: baseline + random + socket ingestion all fired"; \
	else \
		echo "[smoke] engine log tail:"; tail -n 5 $(SMOKE_LOG); \
	fi; \
	exit $$status

clean:
	rm -f $(ENGINE) $(CLIENT) $(DEMO) $(SMOKE_LOG)

.PHONY: all smoke clean
