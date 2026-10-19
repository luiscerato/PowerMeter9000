#include "rtc_clock.h"

#include <Wire.h>
#include <RTClib.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include "pins.h"
#include "diagnostic_logger.h"

static RTC_DS3231 rtc;
static bool rtcPresent = false;
static bool rtcValid = false;
static bool rtcLostPower = true;
static constexpr uint32_t minimumRtcEpoch = 1704067200UL;

// Inicia el bus compartido y valida que el reloj no haya perdido su respaldo.
bool rtcClockBegin()
{
	Wire.begin(pinSDA, pinSCL);
	Wire.setClock(400000);
	rtcPresent = rtc.begin();
	if (!rtcPresent) {
		DIAG_WARN(DiagnosticModule::main, "RTC DS3231 no detectado");
		return false;
	}

	rtcLostPower = rtc.lostPower();
	const uint32_t epoch = rtc.now().unixtime();
	rtcValid = !rtcLostPower && epoch >= minimumRtcEpoch;
	DIAG_INFO(DiagnosticModule::main, "RTC detectado, valido=%u, perdio_respaldo=%u, epoch=%lu",
		rtcValid, rtcLostPower, static_cast<unsigned long>(epoch));
	return rtcValid;
}

// Lee la hora UTC si el RTC fue detectado y su contenido parece válido.
bool rtcClockRead(uint32_t& epoch)
{
	if (!rtcPresent || !rtcValid) return false;
	epoch = rtc.now().unixtime();
	return epoch >= minimumRtcEpoch;
}

// Compara el RTC con el reloj del sistema y corrige desvíos de dos segundos o más.
bool rtcClockSyncFromSystem()
{
	if (!rtcPresent) return false;

	const time_t systemEpoch = time(nullptr);
	if (systemEpoch < minimumRtcEpoch) return false;

	const uint32_t rtcEpoch = rtc.now().unixtime();
	const int64_t drift = static_cast<int64_t>(systemEpoch) - rtcEpoch;
	if (rtcLostPower || !rtcValid || llabs(drift) >= 2) {
		rtc.adjust(DateTime(static_cast<uint32_t>(systemEpoch)));
		rtcLostPower = false;
		rtcValid = true;
		DIAG_INFO(DiagnosticModule::main, "RTC ajustado desde el reloj del sistema, deriva=%llds",
			static_cast<long long>(drift));
	}
	else {
		DIAG_INFO(DiagnosticModule::main, "RTC sincronizado, deriva=%llds",
			static_cast<long long>(drift));
	}
	return true;
}

// Lee el estado del RTC y calcula la diferencia contra la hora UTC del sistema.
bool rtcClockGetInfo(rtcClockInfo& info)
{
	info = {};
	info.present = rtcPresent;
	info.lostPower = rtcLostPower;
	info.systemEpoch = static_cast<uint32_t>(time(nullptr));
	if (!rtcPresent) return false;

	info.rtcEpoch = rtc.now().unixtime();
	info.valid = rtcValid && !rtcLostPower && info.rtcEpoch >= minimumRtcEpoch;
	if (info.valid && info.systemEpoch >= minimumRtcEpoch) {
		info.driftSeconds = static_cast<int32_t>(static_cast<int64_t>(info.systemEpoch) - info.rtcEpoch);
	}
	return true;
}
