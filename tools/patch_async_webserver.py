from pathlib import Path

Import("env")


# Corrige sourcePath antes de compilar: una salida TCP llena debe esperar, sin llamar a malloc(0).
def patchResponseAllocation(sourcePath):
    source = sourcePath.read_text(encoding="utf-8")
    allocation = "    uint8_t *buf = (uint8_t *)malloc(outLen + headLen);"
    guarded = "    if (!outLen) {\n      return 0;\n    }\n\n" + allocation
    if guarded in source:
        return
    if source.count(allocation) != 1:
        raise RuntimeError("Revisar el parche de backpressure para esta version de ESPAsyncWebServer")
    sourcePath.write_text(source.replace(allocation, guarded), encoding="utf-8")
    print("ESPAsyncWebServer: TCP sin espacio espera, sin abortar por malloc(0)")


patchResponseAllocation(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] /
                        "ESPAsyncWebServer" / "src" / "WebResponses.cpp")
