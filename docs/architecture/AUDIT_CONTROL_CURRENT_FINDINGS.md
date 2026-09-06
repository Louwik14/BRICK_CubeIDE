# CONTROL - Current Findings

## CF-001 - Hall double-tap deadline is processed before the ordered Hall/UI event

- `g_patch_pending` is written and consumed by the UI Hall flow, while its
  deadline is sampled and serviced from CONTROL.
- A normal second tap captured within 400 ms can remain in the Hall queue, or
  in the UI queue, while CONTROL reaches the deadline first. CONTROL then
  clears the pending gesture and opens Patch Assign before the second tap is
  consumed; the valid double-tap is lost or reinterpreted as a new first tap.
- The cause is visible in the normal path: `brick6_app_control_process_causes`
  calls `ui_hall_mode_flow_service_pending()` before
  `hall_keyboard_bridge_process()`, and the pending gesture has no single
  serialized owner across CONTROL and UI_SERVICE.
- This violates the frozen true-400-ms Hall double-tap contract. It is an
  ordering/ownership defect, not an impossible-state recovery case or a
  missing assert.
