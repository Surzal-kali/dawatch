# dawatch build — targets per engine.README / main.README §11
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2
LDLIBS  := -lm

# Platform shim: winsock port on Windows, POSIX passthrough otherwise.
# $(OS)=Windows_NT detection works in any shell (cmd, PowerShell); the
# MINGW uname check remains as the MSYS2/Git Bash terminal fallback.
ifeq ($(OS),Windows_NT)
PLATFORM_SRC := engine/platform_win32.c
LDLIBS       += -lws2_32
else
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
PLATFORM_SRC := engine/platform_win32.c
LDLIBS       += -lws2_32
else
PLATFORM_SRC := engine/platform_posix.c
endif
endif

ENGINE  := engine/engine_demo
CLIENT  := honeyjars/canary_client
DEMO    := honeyjars/canary_demo
TEST_RB := engine/test_ringbuffer
TEST_ING := engine/test_ingest
TEST_BAS := engine/test_baseline
WIRE_DEMO := wires/wire_demo
WIRE_TEST := wires/test_wire
WIRE_LOG := dawatch_wire_smoke.log
SMOKE_LOG := dawatch_smoke.log

all: $(ENGINE) $(CLIENT) $(DEMO)

$(ENGINE): engine/engine.c engine/ingest.c engine/ringbuffer.c engine/baseline.c engine/platform.h engine/platform_posix.c engine/platform_win32.c engine/contracts.h engine/ingest.h engine/ringbuffer.h engine/baseline.h
	$(CC) $(CFLAGS) -o $@ engine/engine.c engine/ingest.c engine/ringbuffer.c engine/baseline.c $(PLATFORM_SRC) $(LDLIBS)

$(CLIENT): honeyjars/canary_client.c engine/contracts.h engine/platform.h engine/platform_posix.c engine/platform_win32.c
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_client.c $(PLATFORM_SRC) $(LDLIBS)

$(DEMO): honeyjars/canary_demo.c honeyjars/canary_contact.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ honeyjars/canary_demo.c engine/ringbuffer.c $(PLATFORM_SRC) $(LDLIBS)

# focused C tests — plain asserts, no test framework (gcc + make only)
$(TEST_RB): engine/test_ringbuffer.c engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h engine/platform.h engine/platform_posix.c engine/platform_win32.c
	$(CC) $(CFLAGS) -o $@ engine/test_ringbuffer.c engine/ringbuffer.c $(PLATFORM_SRC) $(LDLIBS)

$(TEST_ING): engine/test_ingest.c engine/ingest.c engine/ringbuffer.c engine/ingest.h engine/ringbuffer.h engine/contracts.h engine/platform.h engine/platform_posix.c engine/platform_win32.c
	$(CC) $(CFLAGS) -o $@ engine/test_ingest.c engine/ingest.c engine/ringbuffer.c $(PLATFORM_SRC) $(LDLIBS)

$(TEST_BAS): engine/test_baseline.c engine/baseline.c engine/baseline.h
	$(CC) $(CFLAGS) -o $@ engine/test_baseline.c engine/baseline.c $(LDLIBS)

test: $(TEST_RB) $(TEST_ING) $(TEST_BAS) $(WIRE_TEST)
	$(TEST_RB)
	$(TEST_ING)
	$(TEST_BAS)
	$(WIRE_TEST)

$(WIRE_DEMO): wires/wire_demo.c wires/wire_flow.c wires/wire_flow.h wires/wire_monitor.c wires/wire_monitor.h wires/wire_source.c wires/wire_source.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h engine/platform.h engine/platform_posix.c engine/platform_win32.c
	$(CC) $(CFLAGS) -o $@ wires/wire_demo.c wires/wire_flow.c wires/wire_monitor.c wires/wire_source.c engine/ringbuffer.c $(PLATFORM_SRC) $(LDLIBS)

$(WIRE_TEST): wires/test_wire.c wires/wire_flow.c wires/wire_flow.h wires/wire_monitor.c wires/wire_monitor.h engine/ringbuffer.c engine/ringbuffer.h engine/contracts.h
	$(CC) $(CFLAGS) -o $@ wires/test_wire.c wires/wire_flow.c wires/wire_monitor.c engine/ringbuffer.c $(LDLIBS)

# engine demo (30s) in background, client connects mid-run, wire demo streams
# its full first-window span, then grep the logs for each trigger line:
# baseline ticks, random burst, genuine event deviation (unblocked by the
# canary split), socket ingestion (canary AND wire), canary-touch ALERT,
# wire cadence detection, and the clean-finish line.
smoke: $(ENGINE) $(CLIENT) $(WIRE_DEMO) test
ifeq ($(OS),Windows_NT)
	@powershell -ExecutionPolicy Bypass -Command \
	"Remove-Item -ErrorAction SilentlyContinue $(SMOKE_LOG), $(SMOKE_LOG).err, $(WIRE_LOG), $(WIRE_LOG).err; \
	$$eng = Start-Process './$(ENGINE)' -RedirectStandardOutput $(SMOKE_LOG) -RedirectStandardError '$(SMOKE_LOG).err' -PassThru -WindowStyle Hidden; \
	$$wire = Start-Process './$(WIRE_DEMO)' -ArgumentList '6' -RedirectStandardOutput $(WIRE_LOG) -RedirectStandardError '$(WIRE_LOG).err' -PassThru -WindowStyle Hidden; \
	$$ok = 0; Start-Sleep -Seconds 1; \
	for ($$i=1; $$i -le 10; $$i++) { if ((Start-Process './$(CLIENT)' -Wait -WindowStyle Hidden -PassThru).ExitCode -eq 0) { $$ok = 1; break } ; Start-Sleep -Milliseconds 500 }; \
	Wait-Process -Id $$eng.Id -ErrorAction SilentlyContinue; \
	Wait-Process -Id $$wire.Id -ErrorAction SilentlyContinue; \
	$$status = 0; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern '\[BASELINE\]' -Quiet)) { Write-Host '[smoke] FAIL: no baseline ticks'; $$status = 1 }; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern '\[RANDOM\]' -Quiet)) { Write-Host '[smoke] FAIL: no random trigger fired'; $$status = 1 }; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern '\[BASELINE\].*event=1' -Quiet)) { Write-Host '[smoke] FAIL: no genuine event deviation fired'; $$status = 1 }; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern 'received contact lane=4' -Quiet)) { Write-Host '[smoke] FAIL: canary client contact not ingested'; $$status = 1 }; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern 'reason=canary touch' -Quiet)) { Write-Host '[smoke] FAIL: canary touch did not route ALERT'; $$status = 1 }; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern 'received contact lane=0' -Quiet)) { Write-Host '[smoke] FAIL: wire lane contacts not ingested'; $$status = 1 }; \
	if (!(Select-String -Path $(WIRE_LOG) -Pattern '\[WIRE\] sent FLOW_TICK' -Quiet)) { Write-Host '[smoke] FAIL: wire lane emitted no window tick'; $$status = 1 }; \
	if (!(Select-String -Path $(WIRE_LOG) -Pattern '\[WIRE\] sent BEACON_SUSPECT' -Quiet)) { Write-Host '[smoke] FAIL: wire lane found no cadence beacon'; $$status = 1 }; \
	if (!(Select-String -Path $(SMOKE_LOG) -Pattern '\[CHECKIN\] Demo completed' -Quiet)) { Write-Host '[smoke] FAIL: engine demo did not finish cleanly'; $$status = 1 }; \
	if ($$ok -eq 0) { Write-Host '[smoke] FAIL: canary_client never connected'; $$status = 1 }; \
	if ($$status -eq 0) { Write-Host '[smoke] PASS: baseline + event + random + canary ALERT + ingestion all fired' } \
	else { Write-Host '[smoke] engine log tail:'; Get-Content $(SMOKE_LOG) -Tail 5 }; \
	exit $$status"
else
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
endif

clean:
ifeq ($(OS),Windows_NT)
	cmd.exe /c "if exist $(ENGINE).exe del /f /q $(ENGINE).exe"
	cmd.exe /c "if exist $(CLIENT).exe del /f /q $(CLIENT).exe"
	cmd.exe /c "if exist $(DEMO).exe del /f /q $(DEMO).exe"
	cmd.exe /c "if exist $(TEST_RB).exe del /f /q $(TEST_RB).exe"
	cmd.exe /c "if exist $(TEST_ING).exe del /f /q $(TEST_ING).exe"
	cmd.exe /c "if exist $(TEST_BAS).exe del /f /q $(TEST_BAS).exe"
	cmd.exe /c "if exist $(WIRE_DEMO).exe del /f /q $(WIRE_DEMO).exe"
	cmd.exe /c "if exist $(WIRE_TEST).exe del /f /q $(WIRE_TEST).exe"
else
	rm -f $(ENGINE) $(CLIENT) $(DEMO) $(TEST_RB) $(TEST_ING) $(TEST_BAS) $(WIRE_DEMO) $(WIRE_TEST) $(SMOKE_LOG) $(WIRE_LOG)
endif

.PHONY: all smoke test clean
