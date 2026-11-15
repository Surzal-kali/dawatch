# dawatch build — targets per engine.README / main.README §11
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
LDLIBS  := -lm

ENGINE  := engine/engine_demo
CLIENT  := honeyjars/canary_client
DEMO    := honeyjars/canary_demo
TEST_RB := engine/test_ringbuffer
TEST_ING := engine/test_ingest
SMOKE_LOG := /tmp/dawatch_smoke.log

all: $(ENGINE) $(CLIENT) $(DEMO)

$(ENGINE): engine/engine.c engine/ingest.c engine/ringbuffer.c engine/contracts.h engine/ingest.h engine/ringbuffer.h
	$(CC) $(CFLAGS) -o $@ engine/engine.c engine/ingest.c engine/ringbuffer.c $(LDLIBS)

$(CLIENT): honeyjars/canary_client.c engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_client.c

$(DEMO): honeyjars/canary_demo.c honeyjars/canary_contact.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_demo.c engine/ringbuffer.c $(LDLIBS)

# focused C tests — plain asserts, no test framework (gcc + make only)
$(TEST_RB): engine/test_ringbuffer.c engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ engine/test_ringbuffer.c engine/ringbuffer.c

$(TEST_ING): engine/test_ingest.c engine/ingest.c engine/ringbuffer.c engine/ingest.h engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ engine/test_ingest.c engine/ingest.c engine/ringbuffer.c

test: $(TEST_RB) $(TEST_ING)
	$(TEST_RB)
	$(TEST_ING)

# engine demo (30s) in background, client connects mid-run, then grep the log
# for each trigger line: baseline ticks, random burst, genuine event deviation
# (unblocked by the canary split), socket ingestion, canary-touch ALERT, and
# the clean-finish line.
smoke: $(ENGINE) $(CLIENT) test
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
	grep -q '\[BASELINE\].*event=1' $(SMOKE_LOG) || { echo "[smoke] FAIL: no genuine event deviation fired"; status=1; }; \
	grep -q 'received contact lane=4' $(SMOKE_LOG) || { echo "[smoke] FAIL: canary client contact not ingested"; status=1; }; \
	grep -q 'reason=canary touch' $(SMOKE_LOG) || { echo "[smoke] FAIL: canary touch did not route ALERT"; status=1; }; \
	grep -q '\[CHECKIN\] Demo completed' $(SMOKE_LOG) || { echo "[smoke] FAIL: engine demo did not finish cleanly"; status=1; }; \
	test $$ok -eq 1 || { echo "[smoke] FAIL: canary_client never connected"; status=1; }; \
	if [ $$status -eq 0 ]; then \
		echo "[smoke] PASS: baseline + event + random + canary ALERT + ingestion all fired"; \
	else \
		echo "[smoke] engine log tail:"; tail -n 5 $(SMOKE_LOG); \
	fi; \
	exit $$status

clean:
	rm -f $(ENGINE) $(CLIENT) $(DEMO) $(TEST_RB) $(TEST_ING) $(SMOKE_LOG)

.PHONY: all smoke test clean
