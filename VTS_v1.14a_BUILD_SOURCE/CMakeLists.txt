cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(da7280_receiver)
target_sources(app PRIVATE src/main.c src/clic.c src/clic_reply.c src/phase_selection.c src/phase_wire.c src/recovery_policy.c src/reconnect_policy.c src/ble_packet.c src/ble_telemetry.c)

# Stage 2: approved FSM and single-owner ESB integration.
target_sources(app PRIVATE src/vts_phase_fsm.c src/vts_phase_controller.c src/vts_phase_link.c src/vts_phase_runtime.c)

# Read-only impedance telemetry; core phase FSM/controller unchanged.
target_sources(app PRIVATE src/vts_impedance.c src/vts_phase_persist.c)

# Read-only v12 payload and immutable snapshot adapter.
target_sources(app PRIVATE src/vts_telemetry_packet.c src/vts_telemetry_snapshot.c)
