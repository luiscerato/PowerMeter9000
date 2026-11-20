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


# Cuenta avisos de congestion de AsyncTCP; sourcePath es el archivo de la dependencia a parchear.
def patchAsyncTcpDiagnostics(sourcePath):
    source = sourcePath.read_text(encoding="utf-8")
    originalSource = source
    queueOriginal = '  return _async_queue && xQueueSend(_async_queue, e, wait) == pdPASS;'
    queueTracked = '''  if (!_async_queue) return false;
  meterAsyncTcpQueueWait(true);
  const bool sent = xQueueSend(_async_queue, e, wait) == pdPASS;
  meterAsyncTcpQueueWait(false);
  return sent;'''
    if queueTracked not in source:
        if source.count(queueOriginal) != 1:
            raise RuntimeError("Revisar instrumentacion de espera AsyncTCP")
        source = source.replace(queueOriginal, queueTracked, 1)
    queueDeclaration = 'extern "C" void meterAsyncTcpQueueWait(bool entering);'
    if queueDeclaration not in source:
        source = source.replace('#include "AsyncTCP.h"', '#include "AsyncTCP.h"\n' + queueDeclaration, 1)
    include = '#include "AsyncTCP.h"'
    declarations = include + "\n\nextern \"C\" void meterAsyncTcpPollCoalesced(uint8_t queueDepth);\n" \
        + 'extern "C" void meterAsyncTcpPollDiscarded(uint8_t queueDepth);\n' \
        + 'extern "C" void meterAsyncTcpError(int8_t errorCode);'
    if declarations not in source:
        oldDeclarations = include + "\n\nextern \"C\" void meterAsyncTcpPollCoalesced(uint8_t queueDepth);\n" \
            + 'extern "C" void meterAsyncTcpPollDiscarded(uint8_t queueDepth);'
        if oldDeclarations in source:
            source = source.replace(oldDeclarations, declarations, 1)
        elif source.count(include) == 1:
            source = source.replace(include, declarations, 1)
        else:
            raise RuntimeError("Revisar el parche de diagnostico para esta version de AsyncTCP")

    changes = {
        '          log_d("coalescing polls, network congestion or async callbacks might be too slow!");':
            '          meterAsyncTcpPollCoalesced(static_cast<uint8_t>(uxQueueMessagesWaiting(_async_queue)));',
        '      log_d("discarding poll due to queue congestion");':
            '      meterAsyncTcpPollDiscarded(static_cast<uint8_t>(uxQueueMessagesWaiting(_async_queue)));',
        '    AsyncClient::_s_error(e->arg, e->error.err);':
            '    meterAsyncTcpError(static_cast<int8_t>(e->error.err));\n    AsyncClient::_s_error(e->arg, e->error.err);'
    }
    for original, replacement in changes.items():
        if replacement in source:
            continue
        if source.count(original) != 1:
            raise RuntimeError("No se encontro el punto de congestión esperado en AsyncTCP")
        source = source.replace(original, replacement, 1)
    if source != originalSource:
        sourcePath.write_text(source, encoding="utf-8")
        print("AsyncTCP: avisos directos sustituidos por contadores filtrados")


patchResponseAllocation(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] /
                        "ESPAsyncWebServer" / "src" / "WebResponses.cpp")
patchAsyncTcpDiagnostics(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] /
                         "AsyncTCP" / "src" / "AsyncTCP.cpp")
