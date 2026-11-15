# dawatch build — targets per engine.README / main.README §11
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
LDLIBS  := -lm

ENGINE  := engine/engine_demo
CLIENT  := honeyjars/canary_client
DEMO    := honeyjars/canary_demo
TEST_RB := engine/test_ringbuffer
TEST_ING := engine/test_ingest
TEST_BAS := engine/test_baseline
WIRE_DEMO := wires/wire_demo
WIRE_TEST := wires/test_wire
WIRE_LOG := /tmp/dawatch_wire_smoke.log
SMOKE_LOG := /tmp/dawatch_smoke.log

all: $(ENGINE) $(CLIENT) $(DEMO)

$(ENGINE): engine/engine.c engine/ingest.c engine/ringbuffer.c engine/baseline.c engine/contracts.h engine/ingest.h engine/ringbuffer.h engine/baseline.h
	$(CC) $(CFLAGS) -o $@ engine/engine.c engine/ingest.c engine/ringbuffer.c engine/baseline.c $(LDLIBS)

$(CLIENT): honeyjars/canary_client.c engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_client.c

$(DEMO): honeyjars/canary_demo.c honeyjars/canary_contact.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_demo.c engine/ringbuffer.c $(LDLIBS)

# focused C tests — plain asserts, no test framework (gcc + make only)
$(TEST_RB): engine/test_ringbuffer.c engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ engine/test_ringbuffer.c engine/ringbuffer.c

$(TEST_ING): engine/test_ingest.c engine/ingest.c engine/ringbuffer.c engine/ingest.h engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ engine/test_ingest.c engine/ingest.c engine/ringbuffer.c

$(TEST_BAS): engine/test_baseline.c engine/baseline.c engine/baseline.h
	$(CC) $(CFLAGS) -o $@ engine/test_baseline.c engine/baseline.c $(LDLIBS)

test: $(TEST_RB) $(TEST_ING) $(TEST_BAS) $(WIRE_TEST)
	$(TEST_RB)
	$(TEST_ING)
	$(TEST_BAS)
	$(WIRE_TEST)

$(WIRE_DEMO): wires/wire_demo.c wires/wire_flow.c wires/wire_flow.h wires/wire_monitor.c wires/wire_monitor.h wires/wire_source.c wires/wire_source.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ wires/wire_demo.c wires/wire_flow.c wires/wire_monitor.c wires/wire_source.c engine/ringbuffer.c $(LDLIBS)

$(WIRE_TEST): wires/test_wire.c wires/wire_flow.c wires/wire_flow.h wires/wire_monitor.c wires/wire_monitor.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ wires/test_wire.c wires/wire_flow.c wires/wire_monitor.c engine/ringbuffer.c $(LDLIBS)

# engine demo (30s) in background, client connects mid-run, wire demo streams
# its full first-window span, then grep the logs for each trigger line:
# baseline ticks, random burst, genuine event deviation (unblocked by the
# canary split), socket ingestion (canary AND wire), canary-touch ALERT,
# wire cadence detection, and the clean-finish line.
smoke: $(ENGINE) $(CLIENT) $(WIRE_DEMO) test
	@rm -f $(SMOKE_LOG) $(WIRE_LOG); \
	$(ENGINE) > $(SMOKE_LOG) 2>&1 & \
	eng=$$!; \
	$(WIRE_DEMO) 6 > $(WIRE_LOG) 2>&1 & \
	wire=$$!; \
	ok=0; \
	sleep 1; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		$(CLIENT) >/dev/null 2>&1 && { ok=1; break; } || sleep 0.5; \
	done; \
	wait $$eng || true; \
	wait $$wire || { echo "[smoke] FAIL: wire demo exited nonzero"; cat $(WIRE_LOG); exit 1; }; \
	status=0; \
	grep -q '\[BASELINE\]' $(SMOKE_LOG) || { echo "[smoke] FAIL: no baseline ticks"; status=1; }; \
	grep -q '\[RANDOM\]' $(SMOKE_LOG) || { echo "[smoke] FAIL: no random trigger fired"; status=1; }; \
	grep -q '\[BASELINE\].*event=1' $(SMOKE_LOG) || { echo "[smoke] FAIL: no genuine event deviation fired"; status=1; }; \
	grep -q 'received contact lane=4' $(SMOKE_LOG) || { echo "[smoke] FAIL: canary client contact not ingested"; status=1; }; \
	grep -q 'reason=canary touch' $(SMOKE_LOG) || { echo "[smoke] FAIL: canary touch did not route ALERT"; status=1; }; \
	grep -q 'received contact lane=0' $(SMOKE_LOG) || { echo "[smoke] FAIL: wire lane contacts not ingested"; status=1; }; \
	grep -q '\[WIRE\] sent FLOW_TICK' $(WIRE_LOG) || { echo "[smoke] FAIL: wire lane emitted no window tick"; status=1; }; \
	grep -q '\[WIRE\] sent BEACON_SUSPECT' $(WIRE_LOG) || { echo "[smoke] FAIL: wire lane found no cadence beacon"; status=1; }; \
	grep -q '\[CHECKIN\] Demo completed' $(SMOKE_LOG) || { echo "[smoke] FAIL: engine demo did not finish cleanly"; status=1; }; \
	test $$ok -eq 1 || { echo "[smoke] FAIL: canary_client never connected"; status=1; }; \
	if [ $$status -eq 0 ]; then \
		echo "[smoke] PASS: baseline + event + random + canary ALERT + ingestion all fired"; \
	else \
		echo "[smoke] engine log tail:"; tail -n 5 $(SMOKE_LOG); \
	fi; \
	exit $$status

clean:
	rm -f $(ENGINE) $(CLIENT) $(DEMO) $(TEST_RB) $(TEST_ING) $(TEST_BAS) $(WIRE_DEMO) $(WIRE_TEST) $(SMOKE_LOG) $(WIRE_LOG)

.PHONY: all smoke test clean
