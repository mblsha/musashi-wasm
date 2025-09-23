export var BreakReason;
(function (BreakReason) {
    BreakReason[BreakReason["None"] = 0] = "None";
    BreakReason[BreakReason["Trace"] = 1] = "Trace";
    BreakReason[BreakReason["InstrHook"] = 2] = "InstrHook";
    BreakReason[BreakReason["JsHook"] = 3] = "JsHook";
    BreakReason[BreakReason["Sentinel"] = 4] = "Sentinel";
})(BreakReason || (BreakReason = {}));
export function getLastBreakReasonFrom(system) {
    const s = system;
    return s._musashi?.getLastBreakReason?.() ?? 0;
}
export function resetLastBreakReasonOn(system) {
    const s = system;
    s._musashi?.resetLastBreakReason?.();
}
//# sourceMappingURL=test-utils.js.map