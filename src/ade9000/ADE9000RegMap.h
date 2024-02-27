/* ================================================================================

     Project      :   ade90xx
     File         :   ade90xx.h
     Description  :   API macros header definition register file.

     Date         :   May 3, 2016

     Copyright (c) 2016 Analog Devices, Inc.  All Rights Reserved.
     This software is proprietary and confidential to Analog Devices, Inc. and
     its licensors.

     This file was auto-generated. Do not make local changes to this file.

   ================================================================================
*/

#pragma ONCE
#include "Arduino.h"

#ifndef ADE9000RegMap
#define ADE9000RegMap

#define ADDR_AIGAIN                          0x00000000            /*  Phase A current gain adjust. */
#define ADDR_AIGAIN0                         0x00000001            /*  If multipoint gain and phase compensation is enabled, with MTEN = 1 in the CONFIG0 register, an additional gain factor, AIGAIN0 through AIGAIN5, is applied based on the AIRMS current rms amplitude and the MTTHR_Lx and MTTHR_Hx register values */
#define ADDR_AIGAIN1                         0x00000002            /*  Phase A Multipoint gain correction factor--see AIGAIN0. */
#define ADDR_AIGAIN2                         0x00000003            /*  Phase A Multipoint gain correction factor--see AIGAIN0. */
#define ADDR_AIGAIN3                         0x00000004            /*  Phase A Multipoint gain correction factor--see AIGAIN0. */
#define ADDR_AIGAIN4                         0x00000005            /*  Phase A Multipoint gain correction factor--see AIGAIN0. */
#define ADDR_APHCAL0                         0x00000006            /*  If multipoint phase and gain calibration is disabled, with MTEN = 0 in the CONFIG0 register, then the APHCAL0 phase compensation is applied. If multipoint phase and gain correction is enabled, with MTEN = 1, then the APHCAL0 through APHCAL4 value is applied, based on the AIRMS current rms amplitude and the MTTHR_Lx and MTTHR_Hx register values */
#define ADDR_APHCAL1                         0x00000007            /*  Phase A Multipoint phase correction factor--see APHCAL0. */
#define ADDR_APHCAL2                         0x00000008            /*  Phase A Multipoint phase correction factor--see APHCAL0. */
#define ADDR_APHCAL3                         0x00000009            /*  Phase A Multipoint phase correction factor--see APHCAL0. */
#define ADDR_APHCAL4                         0x0000000A            /*  Phase A Multipoint phase correction factor--see APHCAL0. */
#define ADDR_AVGAIN                          0x0000000B            /*  Phase A voltage gain adjust. */
#define ADDR_AIRMSOS                         0x0000000C            /*  Phase A current rms offset for filter-based AIRMS calculation */
#define ADDR_AVRMSOS                         0x0000000D            /*  Phase A voltage rms offset for filter-based AVRMS calculation */
#define ADDR_APGAIN                          0x0000000E            /*  Phase A power gain adjust for AWATT, AVA, AVAR, AFWATT, AFVA and AFVAR calculations */
#define ADDR_AWATTOS                         0x0000000F            /*  Phase A total active power offset correction for AWATT calculation */
#define ADDR_AVAROS                          0x00000010            /*  Phase A total reactive power offset correction for AVAR calculation */
#define ADDR_AFWATTOS                        0x00000011            /*  Phase A fundamental active power offset correction for AFWATT calculation. */
#define ADDR_AFVAROS                         0x00000012            /*  Phase A fundamental reactive power offset correction for AFVAR calculation */
#define ADDR_AIFRMSOS                        0x00000013            /*  Phase A current rms offset for fundamental current rms, AIFRMS calculation */
#define ADDR_AVFRMSOS                        0x00000014            /*  Phase A voltage rms offset for fundamental voltage rms, AVFRMS calculation */
#define ADDR_AVRMSONEOS                      0x00000015            /*  Phase A voltage rms offset for fast RMS1/2, AVRMSONE calculation */
#define ADDR_AIRMSONEOS                      0x00000016            /*  Phase A current rms offset for fast RMS1/2, AIRMSONE calculation */
#define ADDR_AVRMS1012OS                     0x00000017            /*  Phase A voltage rms offset for 10/12 cycle rms, AVRMS1012 calculation */
#define ADDR_AIRMS1012OS                     0x00000018            /*  Phase A current rms offset for 10/12 cycle rms, AIRMS1012 calculation */
#define ADDR_BIGAIN                          0x00000020            /*  Phase B current gain adjust. */
#define ADDR_BIGAIN0                         0x00000021            /*  If multipoint gain and phase compensation is enabled, with MTEN = 1 in the CONFIG0 register, an additional gain factor, BIGAIN0 through BIGAIN5, is applied based on the BIRMS current rms amplitude and the MTTHR_Lx and MTTHR_Hx register values */
#define ADDR_BIGAIN1                         0x00000022            /*  Phase B Multipoint gain correction factor--see BIGAIN0. */
#define ADDR_BIGAIN2                         0x00000023            /*  Phase B Multipoint gain correction factor--see BIGAIN0. */
#define ADDR_BIGAIN3                         0x00000024            /*  Phase B Multipoint gain correction factor--see BIGAIN0. */
#define ADDR_BIGAIN4                         0x00000025            /*  Phase B Multipoint gain correction factor--see BIGAIN0. */
#define ADDR_BPHCAL0                         0x00000026            /*  If multipoint phase and gain calibration is disabled, with MTEN = 0 in the CONFIG0 register, then the BPHCAL0 phase compensation is applied. If multipoint phase and gain correction is enabled, with MTEN = 1, then the BPHCAL0 through BPHCAL4 value is applied, based on the BIRMS current rms amplitude and the MTTHR_Lx and MTTHR_Hx register values */
#define ADDR_BPHCAL1                         0x00000027            /*  Phase B Multipoint phase correction factor--see BPHCAL0. */
#define ADDR_BPHCAL2                         0x00000028            /*  Phase B Multipoint phase correction factor--see BPHCAL0. */
#define ADDR_BPHCAL3                         0x00000029            /*  Phase B Multipoint phase correction factor--see BPHCAL0. */
#define ADDR_BPHCAL4                         0x0000002A            /*  Phase B Multipoint phase correction factor--see BPHCAL0. */
#define ADDR_BVGAIN                          0x0000002B            /*  Phase B voltage gain adjust. */
#define ADDR_BIRMSOS                         0x0000002C            /*  Phase B current rms offset for BIRMS calculation */
#define ADDR_BVRMSOS                         0x0000002D            /*  Phase B voltage rms offset for BVRMS calculation */
#define ADDR_BPGAIN                          0x0000002E            /*  Phase B power gain adjust for BWATT, BVA, BVAR, BFWATT, BFVA and BFVAR calculations */
#define ADDR_BWATTOS                         0x0000002F            /*  Phase B total active power offset correction for BWATT calculation */
#define ADDR_BVAROS                          0x00000030            /*  Phase B total reactive power offset correction for BVAR calculation */
#define ADDR_BFWATTOS                        0x00000031            /*  Phase B fundamental active power offset correction for BFWATT calculation. */
#define ADDR_BFVAROS                         0x00000032            /*  Phase B fundamental reactive power offset correction for BFVAR calculation */
#define ADDR_BIFRMSOS                        0x00000033            /*  Phase B current rms offset for fundamental current rms, BIFRMS calculation */
#define ADDR_BVFRMSOS                        0x00000034            /*  Phase B voltage rms offset for fundamental voltage rms, BVFRMS calculation */
#define ADDR_BVRMSONEOS                      0x00000035            /*  Phase B voltage rms offset for fast RMS1/2, BVRMSONE calculation */
#define ADDR_BIRMSONEOS                      0x00000036            /*  Phase B current rms offset for fast RMS1/2, BIRMSONE calculation */
#define ADDR_BVRMS1012OS                     0x00000037            /*  Phase B voltage rms offset for 10/12 cycle rms, BVRMS1012 calculation */
#define ADDR_BIRMS1012OS                     0x00000038            /*  Phase B current rms offset for 10/12 cycle rms, BVRMS1012 calculation */
#define ADDR_CIGAIN                          0x00000040            /*  Phase C current gain adjust. */
#define ADDR_CIGAIN0                         0x00000041            /*  If multipoint gain and phase compensation is enabled, with MTEN = 1 in the CONFIG0 register, an additional gain factor, CIGAIN0 through CIGAIN5, is applied based on the CIRMS current rms amplitude and the MTTHR_Lx and MTTHR_Hx register values */
#define ADDR_CIGAIN1                         0x00000042            /*  Phase C Multipoint gain correction factor--see CIGAIN0. */
#define ADDR_CIGAIN2                         0x00000043            /*  Phase C Multipoint gain correction factor--see CIGAIN0. */
#define ADDR_CIGAIN3                         0x00000044            /*  Phase C Multipoint gain correction factor--see CIGAIN0. */
#define ADDR_CIGAIN4                         0x00000045            /*  Phase C Multipoint gain correction factor--see CIGAIN0. */
#define ADDR_CPHCAL0                         0x00000046            /*  If multipoint phase and gain calibration is disabled, with MTEN = 0 in the CONFIG0 register, then the CPHCAL0 phase compensation is applied. If multipoint phase and gain correction is enabled, with MTEN = 1, then the CPHCAL0 through CPHCAL4 value is applied, based on the CIRMS current rms amplitude and the MTTHR_Lx and MTTHR_Hx register values */
#define ADDR_CPHCAL1                         0x00000047            /*  Phase C Multipoint phase correction factor--see CPHCAL0. */
#define ADDR_CPHCAL2                         0x00000048            /*  Phase C Multipoint phase correction factor--see CPHCAL0. */
#define ADDR_CPHCAL3                         0x00000049            /*  Phase C Multipoint phase correction factor--see CPHCAL0. */
#define ADDR_CPHCAL4                         0x0000004A            /*  Phase C Multipoint phase correction factor--see CPHCAL0. */
#define ADDR_CVGAIN                          0x0000004B            /*  Phase C voltage gain adjust. */
#define ADDR_CIRMSOS                         0x0000004C            /*  Phase C current rms offset for CIRMS calculation */
#define ADDR_CVRMSOS                         0x0000004D            /*  Phase C voltage rms offset for CVRMS calculation */
#define ADDR_CPGAIN                          0x0000004E            /*  Phase C power gain adjust for CWATT, CVA, CVAR, CFWATT, CFVA and CFVAR calculations */
#define ADDR_CWATTOS                         0x0000004F            /*  Phase C total active power offset correction for CWATT calculation */
#define ADDR_CVAROS                          0x00000050            /*  Phase C total reactive power offset correction for CVAR calculation */
#define ADDR_CFWATTOS                        0x00000051            /*  Phase C fundamental active power offset correction for CFWATT calculation. */
#define ADDR_CFVAROS                         0x00000052            /*  Phase C fundamental reactive power offset correction for CFVAR calculation */
#define ADDR_CIFRMSOS                        0x00000053            /*  Phase C current rms offset for fundamental current rms, CIFRMS calculation */
#define ADDR_CVFRMSOS                        0x00000054            /*  Phase C voltage rms offset for fundamental voltage rms, CVFRMS calculation */
#define ADDR_CVRMSONEOS                      0x00000055            /*  Phase C voltage rms offset for fast RMS1/2, CVRMSONE calculation */
#define ADDR_CIRMSONEOS                      0x00000056            /*  Phase C current rms offset for fast RMS1/2, CIRMSONE calculation */
#define ADDR_CVRMS1012OS                     0x00000057            /*  Phase C voltage rms offset for 10/12 cycle rms, CVRMS1012 calculation */
#define ADDR_CIRMS1012OS                     0x00000058            /*  Phase C current rms offset for 10/12 cycle rms, CIRMS1012 calculation */
#define ADDR_CONFIG0                         0x00000060            /*  Configuration register 0 */
#define ADDR_MTTHR_L0                        0x00000061            /*  Multipoint Phase/Gain Threshold. If MTEN = 1 in the CONFIG0 register, the MTGNTHR_Lx and MTGNTHR_Hx registers set up the ranges in which to apply each set of corrections, allowing for hysteresis--see the Multipoint Phase/Gain Correction section for more information */
#define ADDR_MTTHR_L1                        0x00000062            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_L2                        0x00000063            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_L3                        0x00000064            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_L4                        0x00000065            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_H0                        0x00000066            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_H1                        0x00000067            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_H2                        0x00000068            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_H3                        0x00000069            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_MTTHR_H4                        0x0000006A            /*  Multipoint Phase/Gain threshold--see MTTHR_L0 for more information. */
#define ADDR_NIRMSOS                         0x0000006B            /*  Neutral current rms offset for NIRMS calculation */
#define ADDR_ISUMRMSOS                       0x0000006C            /*  Offset correction for ISUMRMS calculation based on the sum of IA+IB+IC+/-IN */
#define ADDR_NIGAIN                          0x0000006D            /*  Neutral current gain adjust. */
#define ADDR_NPHCAL                          0x0000006E            /*  Neutral current phase compensation */
#define ADDR_NIRMSONEOS                      0x0000006F            /*  Neutral current rms offset for fast RMS1/2, NIRMSONE calculation */
#define ADDR_NIRMS1012OS                     0x00000070            /*  Neutral current rms offset for 10/12 cycle rms, NIRMS1012 calculation */
#define ADDR_VNOM                            0x00000071            /*  Nominal phase voltage rms used in the computation of apparent power, xVA, when VNOMx_EN bit is set in the CONFIG0 register */
#define ADDR_DICOEFF                         0x00000072            /*  Value used in the digital integrator algorithm. If the integrator is turned on, with INTEN or ININTEN equal to one in the CONFIG0 register, it is recommended to set this value to 0xFFFFE000. */
#define ADDR_ISUMLVL                         0x00000073            /*  Threshold to compare ISUMRMS against. Configure this register to get a MISMTCH indication in STATUS0 if ISUMRMS exceeds this threshold. */
#define ADDR_AI_PCF                          0x0000020A            /*  Instantaneous Phase A Current Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_AV_PCF                          0x0000020B            /*  Instantaneous Phase A Voltage Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_AIRMS                           0x0000020C            /*  Phase A Filter-based Current rms value, updates at 8ksps */
#define ADDR_AVRMS                           0x0000020D            /*  Phase A Filter-based Voltage rms value, updates at 8ksps */
#define ADDR_AIFRMS                          0x0000020E            /*  Phase A Current Fundamental rms, updates at 8ksps */
#define ADDR_AVFRMS                          0x0000020F            /*  Phase A Voltage Fundamental RMS, updates at 8ksps */
#define ADDR_AWATT                           0x00000210            /*  Phase A Low-pass filtered total active power, updated at 8ksps */
#define ADDR_AVAR                            0x00000211            /*  Phase A Low-pass filtered total reactive power, updated at 8ksps */
#define ADDR_AVA                             0x00000212            /*  Phase A Total apparent power, updated at 8ksps */
#define ADDR_AFWATT                          0x00000213            /*  Phase A Fundamental active power, updated at 8ksps */
#define ADDR_AFVAR                           0x00000214            /*  Phase A Fundamental reactive power, updated at 8ksps */
#define ADDR_AFVA                            0x00000215            /*  Phase A Fundamental apparent power, updated at 8ksps */
#define ADDR_APF                             0x00000216            /*  Phase A Power Factor, updated at 1.024s */
#define ADDR_AVTHD                           0x00000217            /*  Phase A Voltage Total Harmonic Distortion, THD, updated every 1.024s */
#define ADDR_AITHD                           0x00000218            /*  Phase A Current Total Harmonic Distortion, THD, updated every 1.024s */
#define ADDR_AIRMSONE                        0x00000219            /*  Phase A Current fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_AVRMSONE                        0x0000021A            /*  Phase A Voltage fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_AIRMS1012                       0x0000021B            /*  Phase A Current fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_AVRMS1012                       0x0000021C            /*  Phase A Voltage fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_AMTREGION                       0x0000021D            /*  If multipoint gain and phase compensation is enabled, with MTEN = 1 in the CONFIG0 register, these bits indicate which AIGAINx and APHCALx is currently being used */
#define ADDR_BI_PCF                          0x0000022A            /*  Instantaneous Phase B Current Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_BV_PCF                          0x0000022B            /*  Instantaneous Phase B Voltage Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_BIRMS                           0x0000022C            /*  Phase B Filter-based Current rms value, updates at 8ksps */
#define ADDR_BVRMS                           0x0000022D            /*  Phase B Filter-based Voltage rms value, updates at 8ksps */
#define ADDR_BIFRMS                          0x0000022E            /*  Phase B Current Fundamental rms, updates at 8ksps */
#define ADDR_BVFRMS                          0x0000022F            /*  Phase B Voltage Fundamental rms, updates at 8ksps */
#define ADDR_BWATT                           0x00000230            /*  Phase B Low-pass filtered total active power, updated at 8ksps */
#define ADDR_BVAR                            0x00000231            /*  Phase B Low-pass filtered total reactive power, updated at 8ksps */
#define ADDR_BVA                             0x00000232            /*  Phase B Total apparent power, updated at 8ksps */
#define ADDR_BFWATT                          0x00000233            /*  Phase B Fundamental active power, updated at 8ksps */
#define ADDR_BFVAR                           0x00000234            /*  Phase B Fundamental reactive power, updated at 8ksps */
#define ADDR_BFVA                            0x00000235            /*  Phase B Fundamental apparent power, updated at 8ksps */
#define ADDR_BPF                             0x00000236            /*  Phase B Power Factor, updated at 1.024s */
#define ADDR_BVTHD                           0x00000237            /*  Phase B Voltage Total Harmonic Distortion, THD, updated every 1.024s */
#define ADDR_BITHD                           0x00000238            /*  Phase B Current Total Harmonic Distortion, THD, updated every 1.024s */
#define ADDR_BIRMSONE                        0x00000239            /*  Phase B Current fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_BVRMSONE                        0x0000023A            /*  Phase B Voltage fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_BIRMS1012                       0x0000023B            /*  Phase B Current fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_BVRMS1012                       0x0000023C            /*  Phase B Voltage fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_BMTREGION                       0x0000023D            /*  If multipoint gain and phase compensation is enabled, with MTEN = 1 in the COFIG0 register, these bits indicate which BIGAINx and BPHCALx is currently being used */
#define ADDR_CI_PCF                          0x0000024A            /*  Instantaneous Phase C Current Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_CV_PCF                          0x0000024B            /*  Instantaneous Phase C Voltage Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_CIRMS                           0x0000024C            /*  Phase C Filter-based Current rms value, updates at 8ksps */
#define ADDR_CVRMS                           0x0000024D            /*  Phase C Filter-based Voltage rms value, updates at 8ksps */
#define ADDR_CIFRMS                          0x0000024E            /*  Phase C Current Fundamental rms, updates at 8ksps */
#define ADDR_CVFRMS                          0x0000024F            /*  Phase C Voltage Fundamental rms, updates at 8ksps */
#define ADDR_CWATT                           0x00000250            /*  Phase C Low-pass filtered total active power, updated at 8ksps */
#define ADDR_CVAR                            0x00000251            /*  Phase C Low-pass filtered total reactive power, updated at 8ksps */
#define ADDR_CVA                             0x00000252            /*  Phase C Total apparent power, updated at 8ksps */
#define ADDR_CFWATT                          0x00000253            /*  Phase C Fundamental active power, updated at 8ksps */
#define ADDR_CFVAR                           0x00000254            /*  Phase C Fundamental reactive power, updated at 8ksps */
#define ADDR_CFVA                            0x00000255            /*  Phase C Fundamental apparent power, updated at 8ksps */
#define ADDR_CPF                             0x00000256            /*  Phase C Power Factor, updated at 1.024s */
#define ADDR_CVTHD                           0x00000257            /*  Phase C Voltage Total Harmonic Distortion, THD, updated every 1.024s */
#define ADDR_CITHD                           0x00000258            /*  Phase C Current Total Harmonic Distortion, THD, updated every 1.024s */
#define ADDR_CIRMSONE                        0x00000259            /*  Phase C Current fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_CVRMSONE                        0x0000025A            /*  Phase C Voltage fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_CIRMS1012                       0x0000025B            /*  Phase C Current fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_CVRMS1012                       0x0000025C            /*  Phase C Voltage fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_CMTREGION                       0x0000025D            /*  If multipoint gain and phase compensation is enabled, with MTEN = 1 in the CONFIG0 register, these bits indicate which CIGAINx and CPHCALx is currently being used */
#define ADDR_NI_PCF                          0x00000265            /*  Instantaneous Neutral Current Channel Waveform processed by the DSP, at 8ksps */
#define ADDR_NIRMS                           0x00000266            /*  Neutral Current Filter-based rms value */
#define ADDR_NIRMSONE                        0x00000267            /*  Neutral Current fast RMS1/2 calculation, one cycle rms updated every half-cycle */
#define ADDR_NIRMS1012                       0x00000268            /*  Neutral Current fast 10/12 cycle rms calculation.The calculation is done over 10 cycles if SELFREQ = 0 for a 50Hz network or 12 cycles if SELFREQ = 1 for a 60Hz network, in the ACCMODE register. */
#define ADDR_ISUMRMS                         0x00000269            /*  Filter-based RMS based on the sum of IA+IB+IC+/-IN */
#define ADDR_VERSION2                        0x0000026A            /*  This register indicates the version of the metrology algorithms after the user writes RUN=1 to start the measurements */
#define ADDR_AWATT_ACC                       0x000002E5            /*  Phase A accumulated total active power, updated after PWR_TIME 8ksps samples */
#define ADDR_AWATTHR_LO                      0x000002E6            /*  Phase A accumulated total active energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AWATTHR_HI                      0x000002E7            /*  Phase A accumulated total active energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AVAR_ACC                        0x000002EF            /*  Phase A accumulated total reactive power, updated after PWR_TIME 8ksps samples */
#define ADDR_AVARHR_LO                       0x000002F0            /*  Phase A accumulated total reactive energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AVARHR_HI                       0x000002F1            /*  Phase A accumulated total reactive energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AVA_ACC                         0x000002F9            /*  Phase A accumulated total apparent power, updated after PWR_TIME 8ksps samples */
#define ADDR_AVAHR_LO                        0x000002FA            /*  Phase A accumulated total apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AVAHR_HI                        0x000002FB            /*  Phase A accumulated total apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AFWATT_ACC                      0x00000303            /*  Phase A accumulated fundamental active power, updated after PWR_TIME 8ksps samples */
#define ADDR_AFWATTHR_LO                     0x00000304            /*  Phase A accumulated fundamental active energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AFWATTHR_HI                     0x00000305            /*  Phase A accumulated fundamental active energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers. */
#define ADDR_AFVAR_ACC                       0x0000030D            /*  Phase A accumulated fundamental reactive power, updated after PWR_TIME 8ksps samples */
#define ADDR_AFVARHR_LO                      0x0000030E            /*  Phase A accumulated fundamental reactive energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AFVARHR_HI                      0x0000030F            /*  Phase A accumulated fundamental reactive energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AFVA_ACC                        0x00000317            /*  Phase A accumulated fundamental apparent power, updated after PWR_TIME 8ksps samples */
#define ADDR_AFVAHR_LO                       0x00000318            /*  Phase A accumulated fundamental apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_AFVAHR_HI                       0x00000319            /*  Phase A accumulated fundamental apparent energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BWATT_ACC                       0x00000321            /*  Phase B accumulated total active power, updated after PWR_TIME 8ksps samples */
#define ADDR_BWATTHR_LO                      0x00000322            /*  Phase B accumulated total active energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BWATTHR_HI                      0x00000323            /*  Phase B accumulated total active energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BVAR_ACC                        0x0000032B            /*  Phase B accumulated total reactive power, updated after PWR_TIME 8ksps samples */
#define ADDR_BVARHR_LO                       0x0000032C            /*  Phase B accumulated total reactive energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BVARHR_HI                       0x0000032D            /*  Phase B accumulated total reactive energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BVA_ACC                         0x00000335            /*  Phase B accumulated total apparent power, updated after PWR_TIME 8ksps samples */
#define ADDR_BVAHR_LO                        0x00000336            /*  Phase B accumulated total apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BVAHR_HI                        0x00000337            /*  Phase B accumulated total apparent energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BFWATT_ACC                      0x0000033F            /*  Phase B accumulated fundamental active power, updated after PWR_TIME 8ksps samples */
#define ADDR_BFWATTHR_LO                     0x00000340            /*  Phase B accumulated fundamental active energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BFWATTHR_HI                     0x00000341            /*  Phase B accumulated fundamental active energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BFVAR_ACC                       0x00000349            /*  Phase B accumulated fundamental reactive power, updated after PWR_TIME 8ksps samples */
#define ADDR_BFVARHR_LO                      0x0000034A            /*  Phase B accumulated fundamental reactive energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BFVARHR_HI                      0x0000034B            /*  Phase B accumulated fundamental reactive energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BFVA_ACC                        0x00000353            /*  Phase B accumulated fundamental apparent power, updated after PWR_TIME 8ksps samples */
#define ADDR_BFVAHR_LO                       0x00000354            /*  Phase B accumulated fundamental apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_BFVAHR_HI                       0x00000355            /*  Phase B accumulated fundamental apparent energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CWATT_ACC                       0x0000035D            /*  Phase C accumulated total active power, updated after PWR_TIME 8ksps samples */
#define ADDR_CWATTHR_LO                      0x0000035E            /*  Phase C accumulated total active energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CWATTHR_HI                      0x0000035F            /*  Phase C accumulated total active energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CVAR_ACC                        0x00000367            /*  Phase C accumulated total reactive power, updated after PWR_TIME 8ksps samples */
#define ADDR_CVARHR_LO                       0x00000368            /*  Phase C accumulated total reactive energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CVARHR_HI                       0x00000369            /*  Phase C accumulated total reactive energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CVA_ACC                         0x00000371            /*  Phase C accumulated total apparent power, updated after PWR_TIME 8ksps samples */
#define ADDR_CVAHR_LO                        0x00000372            /*  Phase C accumulated total apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CVAHR_HI                        0x00000373            /*  Phase C accumulated total apparent energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CFWATT_ACC                      0x0000037B            /*  Phase C accumulated fundamental active power, updated after PWR_TIME 8ksps samples */
#define ADDR_CFWATTHR_LO                     0x0000037C            /*  Phase C accumulated fundamental active energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CFWATTHR_HI                     0x0000037D            /*  Phase C accumulated fundamental active energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CFVAR_ACC                       0x00000385            /*  Phase C accumulated fundamental reactive power, updated after PWR_TIME 8ksps samples */
#define ADDR_CFVARHR_LO                      0x00000386            /*  Phase C accumulated fundamental reactive energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CFVARHR_HI                      0x00000387            /*  Phase C accumulated fundamental reactive energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CFVA_ACC                        0x0000038F            /*  Phase C accumulated fundamental apparent power, updated after PWR_TIME 8ksps samples */
#define ADDR_CFVAHR_LO                       0x00000390            /*  Phase C accumulated fundamental apparent energy, Least Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_CFVAHR_HI                       0x00000391            /*  Phase C accumulated fundamental apparent energy, Most Significant Bits. Updated according to the settings in EP_CFG and EGY_TIME registers */
#define ADDR_PWATT_ACC                       0x00000397            /*  Accumulated Positive Total Active Power, Most Significant Bits, from AWATT, BWATT and CWATT registers, updated after PWR_TIME 8ksps samples */
#define ADDR_NWATT_ACC                       0x0000039B            /*  Accumulated Negative Total Active Power, Most Significant Bits, from AWATT, BWATT and CWATT registers, updated after PWR_TIME 8ksps samples */
#define ADDR_PVAR_ACC                        0x0000039F            /*  Accumulated Positive Total Reactive Power, Most Significant Bits, from AVAR, BVAR and CVAR registers, updated after PWR_TIME 8ksps samples */
#define ADDR_NVAR_ACC                        0x000003A3            /*  Accumulated Negative Total Reactive Power, Most Significant Bits, from AVAR, BVAR and CVAR registers, updated after PWR_TIME 8ksps samples */
#define ADDR_IPEAK                           0x00000400            /*  Current peak register */
#define ADDR_VPEAK                           0x00000401            /*  Voltage peak register */
#define ADDR_STATUS0                         0x00000402            /*  Status Register 0 */
#define ADDR_STATUS1                         0x00000403            /*  Status Register 1 */
#define ADDR_EVENT_STATUS                    0x00000404            /*  Event Status Register */
#define ADDR_MASK0                           0x00000405            /*  Interrupt Enable Register 0 */
#define ADDR_MASK1                           0x00000406            /*  Interrupt Enable Register 1 */
#define ADDR_EVENT_MASK                      0x00000407            /*  Event Enable Register */
#define ADDR_OILVL                           0x00000409            /*  Over current detection threshold level */
#define ADDR_OIA                             0x0000040A            /*  Phase A overcurrent RMS 1/2 value. If a phase is enabled, with the OC_ENA bit set in the CONFIG3 register and the AIRMSONE is greater than the OILVL threshold then this value is updated. */
#define ADDR_OIB                             0x0000040B            /*  Phase B overcurrent RMS 1/2 value. If a phase is enabled, with the OC_ENB bit set in the CONFIG3 register and the BIRMSONE is greater than the OILVL threshold then this value is updated. */
#define ADDR_OIC                             0x0000040C            /*  Phase C overcurrent RMS 1/2 value. If a phase is enabled, with the OC_ENC bit set in the CONFIG3 register and the CIRMSONE is greater than the OILVL threshold then this value is updated. */
#define ADDR_OIN                             0x0000040D            /*  Neutral Current overcurrent RMS 1/2 value. If enabled, with the OC_ENN bit set in the CONFIG3 register and the NIRMSONE is greater than the OILVL threshold then this value is updated. */
#define ADDR_USER_PERIOD                     0x0000040E            /*  User configured line period value used for resampling, fast rms 1/2 and 10/12 cycle rms when the UPERIOD_SEL bit in the CONFIG2 register is set */
#define ADDR_VLEVEL                          0x0000040F            /*  Register used in the algorithm that computes the fundamental active, reactive and apparent powers as well as the fundemantal IRMS and VRMS */
#define ADDR_DIP_LVL                         0x00000410            /*  Voltage RMS1/2 Dip detection threshold level. */
#define ADDR_DIPA                            0x00000411            /*  Phase A Voltage RMS 1/2 value during a dip condition. */
#define ADDR_DIPB                            0x00000412            /*  Phase B Voltage RMS 1/2 value during a dip condition. */
#define ADDR_DIPC                            0x00000413            /*  Phase C Voltage RMS 1/2 value during a dip condition. */
#define ADDR_SWELL_LVL                       0x00000414            /*  Voltage RMS1/2 Swell detection threshold level. */
#define ADDR_SWELLA                          0x00000415            /*  Phase A Voltage RMS 1/2 value during a swell condition. */
#define ADDR_SWELLB                          0x00000416            /*  Phase B Voltage RMS 1/2 value during a swell condition. */
#define ADDR_SWELLC                          0x00000417            /*  Phase C Voltage RMS 1/2 value during a swell condition. */
#define ADDR_APERIOD                         0x00000418            /*  Line period on Phase A voltage */
#define ADDR_BPERIOD                         0x00000419            /*  Line period on Phase B voltage */
#define ADDR_CPERIOD                         0x0000041A            /*  Line period on Phase C voltage */
#define ADDR_COM_PERIOD                      0x0000041B            /*  Line period measurement on combined signal from Phase A, B, C Voltages */
#define ADDR_ACT_NL_LVL                      0x0000041C            /*  No-load threshold in the total and fundamental active power datapath. */
#define ADDR_REACT_NL_LVL                    0x0000041D            /*  No-load threshold in the total and fundamental reactive power datapath. */
#define ADDR_APP_NL_LVL                      0x0000041E            /*  No-load threshold in the total and fundamental apparent power datapath. */
#define ADDR_PHNOLOAD                        0x0000041F            /*  Phase No-load register */
#define ADDR_WTHR                            0x00000420            /*  Sets the maximum output rate from the digital to frequency converter for the total and fundamental active power for the CF calibration pulse output. It is recommended to write WTHR = 0x0010_0000. */
#define ADDR_VARTHR                          0x00000421            /*  Sets the maximum output rate from the digital to frequency converter for the total and fundamental reactive power for the CF calibration pulse output. It is recommended to write VARTHR = 0x0010_0000. */
#define ADDR_VATHR                           0x00000422            /*  Sets the maximum output rate from the digital to frequency converter for the total and fundamental apparent power for the CF calibration pulse output. It is recommended to write VATHR = 0x0010_0000. */
#define ADDR_LAST_DATA_32                    0x00000423            /*  This register holds the data read or written during the last 32-bit transaction on the SPI port */
#define ADDR_ADC_REDIRECT                    0x00000424            /*  This register allows any ADC output to be redirected to any digital datapath */
#define ADDR_CF_LCFG                         0x00000425            /*  CF calibration pulse width configuration register */
#define ADDR_TEMP_TRIM                       0x00000474            /*  Temperature sensor gain and offset, calculated during the manufacturing process */
#define ADDR_RUN                             0x00000480            /*  Write this register to 1 to start the measurements. */
#define ADDR_CONFIG1                         0x00000481            /*  Configuration register 1 */
#define ADDR_ANGL_VA_VB                      0x00000482            /*  Time between positive to negative zero crossings on Phase A and Phase B Voltages */
#define ADDR_ANGL_VB_VC                      0x00000483            /*  Time between positive to negative zero crossings on Phase B and Phase C Voltages */
#define ADDR_ANGL_VA_VC                      0x00000484            /*  Time between positive to negative zero crossings on Phase A and Phase C Voltages */
#define ADDR_ANGL_VA_IA                      0x00000485            /*  Time between positive to negative zero crossings on Phase A Voltage and Current */
#define ADDR_ANGL_VB_IB                      0x00000486            /*  Time between positive to negative zero crossings on Phase B Voltage and Current */
#define ADDR_ANGL_VC_IC                      0x00000487            /*  Time between positive to negative zero crossings on Phase C Voltage and Current */
#define ADDR_ANGL_IA_IB                      0x00000488            /*  Time between positive to negative zero crossings on Phase A and Phase B Current */
#define ADDR_ANGL_IB_IC                      0x00000489            /*  Time between positive to negative zero crossings on Phase B and Phase C Current */
#define ADDR_ANGL_IA_IC                      0x0000048A            /*  Time between positive to negative zero crossings on Phase A and Phase C Current */
#define ADDR_DIP_CYC                         0x0000048B            /*  Voltage RMS1/2 Dip detection cycle configuration. */
#define ADDR_SWELL_CYC                       0x0000048C            /*  Voltage RMS1/2 Swell detection cycle configuration. */
#define ADDR_OISTATUS                        0x0000048F            /*  Overcurrent Status register */
#define ADDR_CFMODE                          0x00000490            /*  CFx configuration register */
#define ADDR_COMPMODE                        0x00000491            /*  Computation mode register */
#define ADDR_ACCMODE                         0x00000492            /*  Accumulation mode register */
#define ADDR_CONFIG3                         0x00000493            /*  Configuration register 3 */
#define ADDR_CF1DEN                          0x00000494            /*  CF1 denominator register. */
#define ADDR_CF2DEN                          0x00000495            /*  CF2 denominator register. */
#define ADDR_CF3DEN                          0x00000496            /*  CF3 denominator register. */
#define ADDR_CF4DEN                          0x00000497            /*  CF4 denominator register. */
#define ADDR_ZXTOUT                          0x00000498            /*  Zero-crossing timeout configuration register */
#define ADDR_ZXTHRSH                         0x00000499            /*  Voltage Channel Zero-crossing threshold register */
#define ADDR_ZX_LP_SEL                       0x0000049A            /*  This register selects which zero crossing and which line period measurement are used for other calculations */
#define ADDR_SEQ_CYC                         0x0000049C            /*  Number of line cycles used for phase sequence detection. It is recommended to set this register to 1. */
#define ADDR_PHSIGN                          0x0000049D            /*  Power sign register */
#define ADDR_WFB_CFG                         0x000004A0            /*  Waveform Buffer Configuration register */
#define ADDR_WFB_PG_IRQEN                    0x000004A1            /*  This register enables interrupts to occur after specific pages of the waveform buffer have been filled */
#define ADDR_WFB_TRG_CFG                     0x000004A2            /*  This register enables events to trigger a capture in the waveform buffer */
#define ADDR_WFB_TRG_STAT                    0x000004A3            /*  This register indicates the last page which was filled in the waveform buffer and the location of trigger events */
#define ADDR_CONFIG5                         0x000004A4            /*  Configuration register 5 */
#define ADDR_CRC_RSLT                        0x000004A8            /*  This register holds the CRC of configuration registers */
#define ADDR_CRC_SPI                         0x000004A9            /*  This register holds the 16-bit CRC of the data sent out on the MOSI pin during the last SPI register read */
#define ADDR_LAST_DATA_16                    0x000004AC            /*  This register holds the data read or written during the last 16-bit transaction on the SPI port */
#define ADDR_LAST_CMD                        0x000004AE            /*  This register holds the address and read/write operation request (CMD_HDR) for the last transaction on the SPI port */
#define ADDR_CONFIG2                         0x000004AF            /*  Configuration register 2 */
#define ADDR_EP_CFG                          0x000004B0            /*  Energy and power accumulation configuration */
#define ADDR_PWR_TIME                        0x000004B1            /*  Power Update time configuration */
#define ADDR_EGY_TIME                        0x000004B2            /*  Energy accumulation update time configuration */
#define ADDR_CRC_FORCE                       0x000004B4            /*  This register forces an update of the CRC of configuration registers */
#define ADDR_CRC_OPTEN                       0x000004B5            /*  This register selects which registers are optionally included in the configuration register CRC feature */
#define ADDR_TEMP_CFG                        0x000004B6            /*  Temperature sensor configuration register */
#define ADDR_TEMP_RSLT                       0x000004B7            /*  Temperature measurement result */
#define ADDR_PSM2_CFG                        0x000004B8            /*  This register configures settings for the low power PSM2 operating mode. This register value is retained in PSM2 and PSM3 but is rewritten to its default value when entering PSM0. */
#define ADDR_PGA_GAIN                        0x000004B9            /*  This register configures the PGA gain for each ADC */
#define ADDR_CHNL_DIS                        0x000004BA            /*  ADC Channel Enable/Disable */
#define ADDR_WR_LOCK                         0x000004BF            /*  This register enables the configuration lock feature */
#define ADDR_VAR_DIS                         0x000004E0            /*  Enable/disable total reactive power calculation */
#define ADDR_RESERVED1                       0x000004F0            /*  This register is reserved. */
#define ADDR_VERSION                         0x000004FE            /*  Version of ADE9000 IC */
#define ADDR_AI_SINC_DAT                     0x00000500            /*  Current channel A ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_AV_SINC_DAT                     0x00000501            /*  Voltage channel A ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_BI_SINC_DAT                     0x00000502            /*  Current channel B ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_BV_SINC_DAT                     0x00000503            /*  Voltage channel B ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_CI_SINC_DAT                     0x00000504            /*  Current channel C ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_CV_SINC_DAT                     0x00000505            /*  Voltage channel C ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_NI_SINC_DAT                     0x00000506            /*  Neutral current channel ADC waveforms from Sinc4 output, at 32ksps */
#define ADDR_AI_LPF_DAT                      0x00000510            /*  Current channel A ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_AV_LPF_DAT                      0x00000511            /*  Voltage channel A ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_BI_LPF_DAT                      0x00000512            /*  Current channel B ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_BV_LPF_DAT                      0x00000513            /*  Voltage channel B ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_CI_LPF_DAT                      0x00000514            /*  Current channel C ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_CV_LPF_DAT                      0x00000515            /*  Voltage channel C ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_NI_LPF_DAT                      0x00000516            /*  Neutral current channel ADC waveforms from Sinc4 + IIR LPF output, at 8ksps */
#define ADDR_AV_PCF_1                        0x00000600            /*  SPI Burst Read Accessible. Registers organized functionally. See AV_PCF. */
#define ADDR_BV_PCF_1                        0x00000601            /*  SPI Burst Read Accessible. Registers organized functionally. See BV_PCF. */
#define ADDR_CV_PCF_1                        0x00000602            /*  SPI Burst Read Accessible. Registers organized functionally. See CV_PCF. */
#define ADDR_NI_PCF_1                        0x00000603            /*  SPI Burst Read Accessible. Registers organized functionally. See NI_PCF. */
#define ADDR_AI_PCF_1                        0x00000604            /*  SPI Burst Read Accessible. Registers organized functionally. See AI_PCF. */
#define ADDR_BI_PCF_1                        0x00000605            /*  SPI Burst Read Accessible. Registers organized functionally. See BI_PCF. */
#define ADDR_CI_PCF_1                        0x00000606            /*  SPI Burst Read Accessible. Registers organized functionally. See CI_PCF. */
#define ADDR_AIRMS_1                         0x00000607            /*  SPI Burst Read Accessible. Registers organized functionally. See AIRMS. */
#define ADDR_BIRMS_1                         0x00000608            /*  SPI Burst Read Accessible. Registers organized functionally. See BIRMS. */
#define ADDR_CIRMS_1                         0x00000609            /*  SPI Burst Read Accessible. Registers organized functionally. See CIRMS. */
#define ADDR_AVRMS_1                         0x0000060A            /*  SPI Burst Read Accessible. Registers organized functionally. See AVRMS. */
#define ADDR_BVRMS_1                         0x0000060B            /*  SPI Burst Read Accessible. Registers organized functionally. See BVRMS. */
#define ADDR_CVRMS_1                         0x0000060C            /*  SPI Burst Read Accessible. Registers organized functionally. See CVRMS. */
#define ADDR_NIRMS_1                         0x0000060D            /*  SPI Burst Read Accessible. Registers organized functionally. See NIRMS. */
#define ADDR_AWATT_1                         0x0000060E            /*  SPI Burst Read Accessible. Registers organized functionally. See AWATT. */
#define ADDR_BWATT_1                         0x0000060F            /*  SPI Burst Read Accessible. Registers organized functionally. See BWATT. */
#define ADDR_CWATT_1                         0x00000610            /*  SPI Burst Read Accessible. Registers organized functionally. See CWATT. */
#define ADDR_AVA_1                           0x00000611            /*  SPI Burst Read Accessible. Registers organized functionally. See AVA. */
#define ADDR_BVA_1                           0x00000612            /*  SPI Burst Read Accessible. Registers organized functionally. See BVA. */
#define ADDR_CVA_1                           0x00000613            /*  SPI Burst Read Accessible. Registers organized functionally. See CVA. */
#define ADDR_AVAR_1                          0x00000614            /*  SPI Burst Read Accessible. Registers organized functionally. See AVAR. */
#define ADDR_BVAR_1                          0x00000615            /*  SPI Burst Read Accessible. Registers organized functionally. See BVAR. */
#define ADDR_CVAR_1                          0x00000616            /*  SPI Burst Read Accessible. Registers organized functionally. See CVAR. */
#define ADDR_AFVAR_1                         0x00000617            /*  SPI Burst Read Accessible. Registers organized functionally. See AFVAR. */
#define ADDR_BFVAR_1                         0x00000618            /*  SPI Burst Read Accessible. Registers organized functionally. See BFVAR. */
#define ADDR_CFVAR_1                         0x00000619            /*  SPI Burst Read Accessible. Registers organized functionally. See CFVAR. */
#define ADDR_APF_1                           0x0000061A            /*  SPI Burst Read Accessible. Registers organized functionally. See APF. */
#define ADDR_BPF_1                           0x0000061B            /*  SPI Burst Read Accessible. Registers organized functionally. See BPF. */
#define ADDR_CPF_1                           0x0000061C            /*  SPI Burst Read Accessible. Registers organized functionally. See CPF. */
#define ADDR_AVTHD_1                         0x0000061D            /*  SPI Burst Read Accessible. Registers organized functionally. See AVTHD. */
#define ADDR_BVTHD_1                         0x0000061E            /*  SPI Burst Read Accessible. Registers organized functionally. See BVTHD. */
#define ADDR_CVTHD_1                         0x0000061F            /*  SPI Burst Read Accessible. Registers organized functionally. See CVTHD. */
#define ADDR_AITHD_1                         0x00000620            /*  SPI Burst Read Accessible. Registers organized functionally. See AITHD. */
#define ADDR_BITHD_1                         0x00000621            /*  SPI Burst Read Accessible. Registers organized functionally. See BITHD. */
#define ADDR_CITHD_1                         0x00000622            /*  SPI Burst Read Accessible. Registers organized functionally. See CITHD. */
#define ADDR_AFWATT_1                        0x00000623            /*  SPI Burst Read Accessible. Registers organized functionally. See AFWATT. */
#define ADDR_BFWATT_1                        0x00000624            /*  SPI Burst Read Accessible. Registers organized functionally. See BFWATT. */
#define ADDR_CFWATT_1                        0x00000625            /*  SPI Burst Read Accessible. Registers organized functionally. See CFWATT. */
#define ADDR_AFVA_1                          0x00000626            /*  SPI Burst Read Accessible. Registers organized functionally. See AFVA. */
#define ADDR_BFVA_1                          0x00000627            /*  SPI Burst Read Accessible. Registers organized functionally. See BFVA. */
#define ADDR_CFVA_1                          0x00000628            /*  SPI Burst Read Accessible. Registers organized functionally. See CFVA. */
#define ADDR_AFIRMS_1                        0x00000629            /*  SPI Burst Read Accessible. Registers organized functionally. See AFIRMS. */
#define ADDR_BFIRMS_1                        0x0000062A            /*  SPI Burst Read Accessible. Registers organized functionally. See BFIRMS. */
#define ADDR_CFIRMS_1                        0x0000062B            /*  SPI Burst Read Accessible. Registers organized functionally. See CFIRMS. */
#define ADDR_AFVRMS_1                        0x0000062C            /*  SPI Burst Read Accessible. Registers organized functionally. See AFVRMS. */
#define ADDR_BFVRMS_1                        0x0000062D            /*  SPI Burst Read Accessible. Registers organized functionally. See BFVRMS. */
#define ADDR_CFVRMS_1                        0x0000062E            /*  SPI Burst Read Accessible. Registers organized functionally. See CFVRMS. */
#define ADDR_AIRMSONE_1                      0x0000062F            /*  SPI Burst Read Accessible. Registers organized functionally. See AIRMSONE. */
#define ADDR_BIRMSONE_1                      0x00000630            /*  SPI Burst Read Accessible. Registers organized functionally. See BIRMSONE. */
#define ADDR_CIRMSONE_1                      0x00000631            /*  SPI Burst Read Accessible. Registers organized functionally. See CIRMSONE. */
#define ADDR_AVRMSONE_1                      0x00000632            /*  SPI Burst Read Accessible. Registers organized functionally. See AVRMSONE. */
#define ADDR_BVRMSONE_1                      0x00000633            /*  SPI Burst Read Accessible. Registers organized functionally. See BVRMSONE. */
#define ADDR_CVRMSONE_1                      0x00000634            /*  SPI Burst Read Accessible. Registers organized functionally. See CVRMSONE. */
#define ADDR_NIRMSONE_1                      0x00000635            /*  SPI Burst Read Accessible. Registers organized functionally. See NIRMSONE. */
#define ADDR_AIRMS1012_1                     0x00000636            /*  SPI Burst Read Accessible. Registers organized functionally. See AIRMS1012. */
#define ADDR_BIRMS1012_1                     0x00000637            /*  SPI Burst Read Accessible. Registers organized functionally. See BIRMS1012. */
#define ADDR_CIRMS1012_1                     0x00000638            /*  SPI Burst Read Accessible. Registers organized functionally. See CIRMS1012. */
#define ADDR_AVRMS1012_1                     0x00000639            /*  SPI Burst Read Accessible. Registers organized functionally. See AVRMS1012. */
#define ADDR_BVRMS1012_1                     0x0000063A            /*  SPI Burst Read Accessible. Registers organized functionally. See BVRMS1012. */
#define ADDR_CVRMS1012_1                     0x0000063B            /*  SPI Burst Read Accessible. Registers organized functionally. See CVRMS1012. */
#define ADDR_NIRMS1012_1                     0x0000063C            /*  SPI Burst Read Accessible. Registers organized functionally. See NIRMS1012. */
#define ADDR_AV_PCF_2                        0x00000680            /*  SPI Burst Read Accessible. Registers organized by phase. See AV_PCF. */
#define ADDR_AI_PCF_2                        0x00000681            /*  SPI Burst Read Accessible. Registers organized by phase. See AI_PCF. */
#define ADDR_AIRMS_2                         0x00000682            /*  SPI Burst Read Accessible. Registers organized by phase. See AIRMS. */
#define ADDR_AVRMS_2                         0x00000683            /*  SPI Burst Read Accessible. Registers organized by phase. See AVRMS. */
#define ADDR_AWATT_2                         0x00000684            /*  SPI Burst Read Accessible. Registers organized by phase. See AWATT. */
#define ADDR_AVA_2                           0x00000685            /*  SPI Burst Read Accessible. Registers organized by phase. See AVA. */
#define ADDR_AVAR_2                          0x00000686            /*  SPI Burst Read Accessible. Registers organized by phase. See AVAR. */
#define ADDR_AFVAR_2                         0x00000687            /*  SPI Burst Read Accessible. Registers organized by phase. See AFVAR. */
#define ADDR_APF_2                           0x00000688            /*  SPI Burst Read Accessible. Registers organized by phase. See APF. */
#define ADDR_AVTHD_2                         0x00000689            /*  SPI Burst Read Accessible. Registers organized by phase. See AVTHD. */
#define ADDR_AITHD_2                         0x0000068A            /*  SPI Burst Read Accessible. Registers organized by phase. See AITHD. */
#define ADDR_AFWATT_2                        0x0000068B            /*  SPI Burst Read Accessible. Registers organized by phase. See AFWATT. */
#define ADDR_AFVA_2                          0x0000068C            /*  SPI Burst Read Accessible. Registers organized by phase. See AFVA. */
#define ADDR_AFIRMS_2                        0x0000068D            /*  SPI Burst Read Accessible. Registers organized by phase. See AFIRMS. */
#define ADDR_AFVRMS_2                        0x0000068E            /*  SPI Burst Read Accessible. Registers organized by phase. See AFVRMS. */
#define ADDR_AIRMSONE_2                      0x0000068F            /*  SPI Burst Read Accessible. Registers organized by phase. See AIRMSONE. */
#define ADDR_AVRMSONE_2                      0x00000690            /*  SPI Burst Read Accessible. Registers organized by phase. See AVRMSONE. */
#define ADDR_AIRMS1012_2                     0x00000691            /*  SPI Burst Read Accessible. Registers organized by phase. See AIRMS1012. */
#define ADDR_AVRMS1012_2                     0x00000692            /*  SPI Burst Read Accessible. Registers organized by phase. See AVRMS1012. */
#define ADDR_BV_PCF_2                        0x00000693            /*  SPI Burst Read Accessible. Registers organized by phase. See BV_PCF. */
#define ADDR_BI_PCF_2                        0x00000694            /*  SPI Burst Read Accessible. Registers organized by phase. See BI_PCF. */
#define ADDR_BIRMS_2                         0x00000695            /*  SPI Burst Read Accessible. Registers organized by phase. See BIRMS. */
#define ADDR_BVRMS_2                         0x00000696            /*  SPI Burst Read Accessible. Registers organized by phase. See BVRMS. */
#define ADDR_BWATT_2                         0x00000697            /*  SPI Burst Read Accessible. Registers organized by phase. See BWATT. */
#define ADDR_BVA_2                           0x00000698            /*  SPI Burst Read Accessible. Registers organized by phase. See BVA. */
#define ADDR_BVAR_2                          0x00000699            /*  SPI Burst Read Accessible. Registers organized by phase. See BVAR. */
#define ADDR_BFVAR_2                         0x0000069A            /*  SPI Burst Read Accessible. Registers organized by phase. See BFVAR. */
#define ADDR_BPF_2                           0x0000069B            /*  SPI Burst Read Accessible. Registers organized by phase. See BPF. */
#define ADDR_BVTHD_2                         0x0000069C            /*  SPI Burst Read Accessible. Registers organized by phase. See BVTHD. */
#define ADDR_BITHD_2                         0x0000069D            /*  SPI Burst Read Accessible. Registers organized by phase. See BITHD. */
#define ADDR_BFWATT_2                        0x0000069E            /*  SPI Burst Read Accessible. Registers organized by phase. See BFWATT. */
#define ADDR_BFVA_2                          0x0000069F            /*  SPI Burst Read Accessible. Registers organized by phase. See BFVA. */
#define ADDR_BFIRMS_2                        0x000006A0            /*  SPI Burst Read Accessible. Registers organized by phase. See BFIRMS. */
#define ADDR_BFVRMS_2                        0x000006A1            /*  SPI Burst Read Accessible. Registers organized by phase. See BFVRMS. */
#define ADDR_BIRMSONE_2                      0x000006A2            /*  SPI Burst Read Accessible. Registers organized by phase. See BIRMSONE. */
#define ADDR_BVRMSONE_2                      0x000006A3            /*  SPI Burst Read Accessible. Registers organized by phase. See BVRMSONE. */
#define ADDR_BIRMS1012_2                     0x000006A4            /*  SPI Burst Read Accessible. Registers organized by phase. See BIRMS1012. */
#define ADDR_BVRMS1012_2                     0x000006A5            /*  SPI Burst Read Accessible. Registers organized by phase. See BVRMS1012. */
#define ADDR_CV_PCF_2                        0x000006A6            /*  SPI Burst Read Accessible. Registers organized by phase. See CV_PCF. */
#define ADDR_CI_PCF_2                        0x000006A7            /*  SPI Burst Read Accessible. Registers organized by phase. See CI_PCF. */
#define ADDR_CIRMS_2                         0x000006A8            /*  SPI Burst Read Accessible. Registers organized by phase. See CIRMS. */
#define ADDR_CVRMS_2                         0x000006A9            /*  SPI Burst Read Accessible. Registers organized by phase. See CVRMS. */
#define ADDR_CWATT_2                         0x000006AA            /*  SPI Burst Read Accessible. Registers organized by phase. See CWATT. */
#define ADDR_CVA_2                           0x000006AB            /*  SPI Burst Read Accessible. Registers organized by phase. See CVA. */
#define ADDR_CVAR_2                          0x000006AC            /*  SPI Burst Read Accessible. Registers organized by phase. See CVAR. */
#define ADDR_CFVAR_2                         0x000006AD            /*  SPI Burst Read Accessible. Registers organized by phase. See CFVAR. */
#define ADDR_CPF_2                           0x000006AE            /*  SPI Burst Read Accessible. Registers organized by phase. See CPF. */
#define ADDR_CVTHD_2                         0x000006AF            /*  SPI Burst Read Accessible. Registers organized by phase. See CVTHD. */
#define ADDR_CITHD_2                         0x000006B0            /*  SPI Burst Read Accessible. Registers organized by phase. See CITHD. */
#define ADDR_CFWATT_2                        0x000006B1            /*  SPI Burst Read Accessible. Registers organized by phase. See CFWATT. */
#define ADDR_CFVA_2                          0x000006B2            /*  SPI Burst Read Accessible. Registers organized by phase. See CFVA. */
#define ADDR_CFIRMS_2                        0x000006B3            /*  SPI Burst Read Accessible. Registers organized by phase. See CFIRMS. */
#define ADDR_CFVRMS_2                        0x000006B4            /*  SPI Burst Read Accessible. Registers organized by phase. See CFVRMS. */
#define ADDR_CIRMSONE_2                      0x000006B5            /*  SPI Burst Read Accessible. Registers organized by phase. See CIRMSONE. */
#define ADDR_CVRMSONE_2                      0x000006B6            /*  SPI Burst Read Accessible. Registers organized by phase. See CVRMSONE. */
#define ADDR_CIRMS1012_2                     0x000006B7            /*  SPI Burst Read Accessible. Registers organized by phase. See CIRMS1012. */
#define ADDR_CVRMS1012_2                     0x000006B8            /*  SPI Burst Read Accessible. Registers organized by phase. See CVRMS1012. */
#define ADDR_NI_PCF_2                        0x000006B9            /*  SPI Burst Read Accessible. Registers organized by phase. See NI_PCF. */
#define ADDR_NIRMS_2                         0x000006BA            /*  SPI Burst Read Accessible. Registers organized by phase. See NIRMS. */
#define ADDR_NIRMSONE_2                      0x000006BB            /*  SPI Burst Read Accessible. Registers organized by phase. See NIRMSONE. */
#define ADDR_NIRMS1012_2                     0x000006BC            /*  SPI Burst Read Accessible. Registers organized by phase. See NIRMS1012. */




// Defines for CONFIG0 register
#define ADE_CONFIG0_BITS_DISRPLPF         0x00002000  // Set this bit to disable the low-pass filter in the total reactive power datapath.
#define ADE_CONFIG0_BITS_DISAPLPF         0x00001000  // Set this bit to disable the low-pass filter in the total active power datapath.
#define ADE_CONFIG0_BITS_ININTEN          0x00000800  // Set this bit to enable the digital integrator in the neutral current channel.
#define ADE_CONFIG0_BITS_VNOMC_EN         0x00000400  // Set this bit to use the nominal phase voltage rms, VNOM, in the computation of Phase C total apparent power, CVA.
#define ADE_CONFIG0_BITS_VNOMB_EN         0x00000200  // Set this bit to use the nominal phase voltage rms, VNOM, in the computation of Phase B total apparent power, BVA.
#define ADE_CONFIG0_BITS_VNOMA_EN         0x00000100  // Set this bit to use the nominal phase voltage rms, VNOM, in the computation of Phase A total apparent power, AVA.
#define ADE_CONFIG0_BITS_RMS_SRC_SEL      0x00000080  // This bit selects which samples are used for the rms½ and 10 cycle rms/12 cycle rms calculation.
#define ADE_CONFIG0_BITS_ZX_SRC_SEL       0x00000040  // This bit selects whether data going into the zero-crossing detection circuit comes before the high-pass filter, integrator, and phase compensation or afterwards.
#define ADE_CONFIG0_BITS_INTEN            0x00000020  // Set this bit to enable the integrators in the phase current channels.
#define ADE_CONFIG0_BITS_MTEN             0x00000010  // Set this bit to enable multipoint phase and gain compensation.
#define ADE_CONFIG0_BITS_HPFDIS           0x00000008  // Set this bit to disable high-pass filters in all the voltage and current channels.
#define ADE_CONFIG0_BITS_ISUM_CFG         0x00000003  // ISUM calculation configuration.


// Struct for CONFIG0 register
typedef union {
  struct {
    uint32_t ISUM_CFG : 2;      // Bits [1:0]: ISUM calculation configuration
    uint32_t RESERVED : 1;      // Bit 2: RESERVED
    uint32_t HPFDIS : 1;        // Bit 3: Set this bit to disable high-pass filters in all the voltage and current channels
    uint32_t MTEN : 1;          // Bit 4: Set this bit to enable multipoint phase and gain compensation
    uint32_t INTEN : 1;         // Bit 5: Set this bit to enable the integrators in the phase current channels
    uint32_t ZX_SRC_SEL : 1;    // Bit 6: This bit selects whether data going into the zero-crossing detection circuit comes before or after the high-pass filter, integrator, and phase compensation
    uint32_t RMS_SRC_SEL : 1;   // Bit 7: This bit selects which samples are used for the rms and 10 cycle rms/12 cycle rms calculation
    uint32_t VNOMA_EN : 1;      // Bit 8: Set this bit to use the nominal phase voltage rms, VNOM, in the computation of Phase A total apparent power
    uint32_t VNOMB_EN : 1;      // Bit 9: Set this bit to use the nominal phase voltage rms, VNOM, in the computation of Phase B total apparent power
    uint32_t VNOMC_EN : 1;      // Bit 10: Set this bit to use the nominal phase voltage rms, VNOM, in the computation of Phase C total apparent power
    uint32_t ININTEN : 1;       // Bit 11: Set this bit to enable the digital integrator in the neutral current channel
    uint32_t DISAPLPF : 1;      // Bit 12: Set this bit to disable the low-pass filter in the total active power datapath
    uint32_t DISRPLPF : 1;      // Bit 13: Set this bit to disable the low-pass filter in the total reactive power datapath
    uint32_t RESERVED_1 : 18;   // Bits [31:14]: RESERVED
  };
  uint32_t raw;
} ADE_CONFIG0_t;



// Struct for AMTREGION register
typedef union {
  struct {
    unsigned AREGION : 4;     // Bits [3:0]: Indicates which AIGAINx and APHCALx is currently being used
  };
  uint32_t raw;
} ADE_AMTREGION_t;

// Struct for BMTREGION register
typedef union {
  struct {
    unsigned BREGION : 4;     // Bits [3:0]: Indicates which BIGAINx and BPHCALx is currently being used
  };
  uint32_t raw;
} ADE_BMTREGION_t;

// Struct for CMTREGION register
typedef union {
  struct {
    unsigned CREGION : 4;     // Bits [3:0]: Indicates which CIGAINx and CPHCALx is currently being used
  };
  uint32_t raw;
} ADE_CMTREGION_t;


// Struct for IPEAK register
typedef union {
  struct {
    unsigned IPEAKVAL : 24;    // Bits [23:0]: Absolute value of the peak current
    unsigned IPPHASE : 3;     // Bits [26:24]: Indicates which phases generate the IPEAKVAL value
  };
  uint32_t raw;
} ADE_IPEAK_t;

// Struct for VPEAK register
typedef union {
  struct {
    unsigned VPEAKVAL : 24;    // Bits [23:0]: Absolute value of the peak voltage
    unsigned VPPHASE : 3;     // Bits [26:24]: Indicates which phases generate the VPEAKVAL value
  };
  uint32_t raw;
} ADE_VPEAK_t;

// Defines for STATUS0 register
#define ADE_STATUS0_BITS_RESERVED_0        0xFC000000  // RESERVED: Reserved bits
#define ADE_STATUS0_BITS_TEMP_RDY           0x02000000  // This bit goes high to indicate when a new temperature measurement is available.
#define ADE_STATUS0_BITS_MISMTCH            0x01000000  // This bit is set to indicate a change in the relationship between ISUMRMS and ISUMLVL.
#define ADE_STATUS0_BITS_COH_WFB_FULL       0x00800000  // This bit is set when the waveform buffer is full with resampled data, which is selected when WF_CAP_SEL = 0 in the WFB_CFG register.
#define ADE_STATUS0_BITS_WFB_TRIG           0x00400000  // This bit is set when one of the events configured in WFB_TRIG_CFG occurs.
#define ADE_STATUS0_BITS_THD_PF_RDY         0x00200000  // This bit goes high to indicate when the THD and power factor measurements update, every 1.024 sec.
#define ADE_STATUS0_BITS_RMS1012RDY         0x00100000  // This bit is set when the 10 cycle rms/12 cycle rms values update.
#define ADE_STATUS0_BITS_RMSONERDY          0x00080000  // This bit is set when the fast rms½ rms values update.
#define ADE_STATUS0_BITS_PWRRDY             0x00040000  // This bit is set when the power values in the xWATT_ACC, xVA_ACC, xVAR_ACC, xFWATT_ACC, xFVA_ACC, and xFVAR_ACC registers update, after PWR_TIME 8 kSPS samples.
#define ADE_STATUS0_BITS_PAGE_FULL          0x00020000  // This bit is set when a page enabled in the WFB_PG_IRQEN register is filled with fixed data rate samples, when WF_CAP_SEL bit in the WFB_CFG register is equal to zero.
#define ADE_STATUS0_BITS_WFB_TRIG_IRQ       0x00010000  // This bit is set when the waveform buffer stops filling after an event configured in WFB_TRIG_CFG occurs. This happens with fixed data rate samples only, when WF_CAP_SEL bit in the WFB_CFG register is equal to zero.
#define ADE_STATUS0_BITS_DREADY             0x00008000  // This bit is set when new waveform samples are ready. The update rate depends on the data selected in the WF_SRC bits in the WFB_CFG register.
#define ADE_STATUS0_BITS_CF4                0x00004000  // This bit is set when a CF4 pulse is issued, when the CF4 pin goes from a high to low state.
#define ADE_STATUS0_BITS_CF3                0x00002000  // This bit is set when a CF3 pulse is issued, when the CF3 pin goes from a high to low state.
#define ADE_STATUS0_BITS_CF2                0x00001000  // This bit is set when a CF2 pulse is issued, when the CF2 pin goes from a high to low state.
#define ADE_STATUS0_BITS_CF1                0x00000800  // This bit is set when a CF1 pulse is issued, when the CF1 pin goes from a high to low state.
#define ADE_STATUS0_BITS_REVPSUM4           0x00000400  // This bit is set to indicate if the CF4 polarity changed sign. For example, if the last CF4 pulse was positive reactive energy and the next CF4 pulse is negative reactive energy, the REVPSUM4 bit is set. This bit is updated when a CF4 pulse is output, when the CF4 pin goes from high to low.
#define ADE_STATUS0_BITS_REVPSUM3           0x00000200  // This bit is set to indicate if the CF3 polarity changed sign. See REVPSUM4.
#define ADE_STATUS0_BITS_REVPSUM2           0x00000100  // This bit is set to indicate if the CF2 polarity changed sign. See REVPSUM4.
#define ADE_STATUS0_BITS_REVPSUM1           0x00000080  // This bit is set to indicate if the CF1 polarity changed sign. See REVPSUM4.
#define ADE_STATUS0_BITS_REVRPC             0x00000040  // This bit indicates if the Phase C total or fundamental reactive power has changed sign. The PWR_SIGN_SEL bit in the EP_CFG register selects whether total or fundamental reactive power is monitored. This bit is updated when the power values in the xVAR_ACC and xFVAR_ACC registers update, after PWR_TIME 8 kSPS samples.
#define ADE_STATUS0_BITS_REVRPB             0x00000020  // This bit indicates if the Phase B total or fundamental reactive power has changed sign. See REVRPC.
#define ADE_STATUS0_BITS_REVRPA             0x00000010  // This bit indicates if the Phase A total or fundamental reactive power has changed sign. See REVRPC.
#define ADE_STATUS0_BITS_REVAPC             0x00000008  // This bit indicates if the Phase C total or fundamental active power has changed sign. The PWR_SIGN_SEL bit in the EP_CFG register selects whether total or fundamental active power is monitored. This bit is updated when the power values in the xWATT_ACC and xFWATT_ACC registers update, after PWR_TIME 8 kSPS samples.
#define ADE_STATUS0_BITS_REVAPB             0x00000004  // This bit indicates if the Phase B total or fundamental active power has changed sign. See REVAPC.
#define ADE_STATUS0_BITS_REVAPA             0x00000002  // This bit indicates if the Phase A total or fundamental active power has changed sign. See REVAPC.
#define ADE_STATUS0_BITS_EGYRDY             0x00000001  // This bit is set when the power values in the xWATTHR xVAHR, xVARHR, xFVARHR, xFWATTHR, xFVAHR registers update, after EGY_TIME 8 kSPS samples or line cycles, depending on the EGY_TMR_MODE bit in the EP_CFG register.

// Definición de la estructura ADE_STATUS0_t
typedef union {
  struct {
    unsigned EGYRDY : 1;          // EGYRDY: Power values update ready
    unsigned REVAPA : 1;          // REVAPA: Phase A total/fundamental active power change sign
    unsigned REVAPB : 1;          // REVAPB: Phase B total/fundamental active power change sign
    unsigned REVAPC : 1;          // REVAPC: Phase C total/fundamental active power change sign
    unsigned REVRPA : 1;          // REVRPA: Phase A total/fundamental reactive power change sign
    unsigned REVRPB : 1;          // REVRPB: Phase B total/fundamental reactive power change sign
    unsigned REVRPC : 1;          // REVRPC: Phase C total/fundamental reactive power change sign
    unsigned REVPSUM1 : 1;        // REVPSUM1: CF1 polarity change sign
    unsigned REVPSUM2 : 1;        // REVPSUM2: CF2 polarity change sign
    unsigned REVPSUM3 : 1;        // REVPSUM3: CF3 polarity change sign
    unsigned REVPSUM4 : 1;        // REVPSUM4: CF4 polarity change sign
    unsigned CF1 : 1;             // CF1: CF1 pulse issued
    unsigned CF2 : 1;             // CF2: CF2 pulse issued
    unsigned CF3 : 1;             // CF3: CF3 pulse issued
    unsigned CF4 : 1;             // CF4: CF4 pulse issued
    unsigned DREADY : 1;          // DREADY: New waveform samples ready
    unsigned WFB_TRIG_IRQ : 1;    // WFB_TRIG_IRQ: Waveform buffer trigger IRQ
    unsigned PAGE_FULL : 1;       // PAGE_FULL: Page filled in waveform buffer
    unsigned PWRRDY : 1;          // PWRRDY: Power values update ready
    unsigned RMSONERDY : 1;       // RMSONERDY: Fast rms½ rms values update
    unsigned RMS1012RDY : 1;      // RMS1012RDY: 10 cycle rms/12 cycle rms values update
    unsigned THD_PF_RDY : 1;     // THD_PF_RDY: THD and power factor measurements update
    unsigned WFB_TRIG : 1;        // WFB_TRIG: Waveform buffer trigger
    unsigned COH_WFB_FULL : 1;    // COH_WFB_FULL: Waveform buffer full with resampled data
    unsigned MISMTCH : 1;         // MISMTCH: Relationship change between ISUMRMS and ISUMLVL
    unsigned TEMP_RDY : 1;        // TEMP_RDY: New temperature measurement available
    unsigned RESERVED : 6;        // RESERVED: Reserved bits
  };
  uint32_t raw;
} ADE_STATUS0_t;

// Defines for STATUS1 register
#define ADE_STATUS1_BITS_ERROR3      0x80000000 // ERROR3: Indicates an error and generates a nonmaskable interrupt
#define ADE_STATUS1_BITS_ERROR2      0x40000000 // ERROR2: Indicates that an error was detected and corrected
#define ADE_STATUS1_BITS_ERROR1      0x20000000 // ERROR1: Indicates an error and generates a nonmaskable interrupt
#define ADE_STATUS1_BITS_ERROR0      0x10000000 // ERROR0: Indicates an error and generates a nonmaskable interrupt
#define ADE_STATUS1_BITS_CRC_DONE    0x08000000 // CRC_DONE: Set to indicate when the configuration register CRC calculation is complete
#define ADE_STATUS1_BITS_CRC_CHG     0x04000000 // CRC_CHG: Set if any of the registers monitored by the configuration register CRC change value
#define ADE_STATUS1_BITS_DIPC        0x02000000 // DIPC: Set to indicates Phase C voltage entered or exited a dip condition
#define ADE_STATUS1_BITS_DIPB        0x01000000 // DIPB: Set to indicates Phase B voltage entered or exited a dip condition
#define ADE_STATUS1_BITS_DIPA        0x00800000 // DIPA: Set to indicates Phase A voltage entered or exited a dip condition
#define ADE_STATUS1_BITS_SWELLC      0x00400000 // SWELLC: Set to indicates Phase C voltage entered or exited a swell condition
#define ADE_STATUS1_BITS_SWELLB      0x00200000 // SWELLB: Set to indicates Phase B voltage entered or exited a swell condition
#define ADE_STATUS1_BITS_SWELLA      0x00100000 // SWELLA: Set to indicates Phase A voltage entered or exited a swell condition
#define ADE_STATUS1_BITS_SEQERR      0x00040000 // SEQERR: Set to indicate a phase sequence error on the Phase voltage zero crossings
#define ADE_STATUS1_BITS_OI          0x00020000 // OI: Set to indicate that an overcurrent event occurred on one of the phases
#define ADE_STATUS1_BITS_RSTDONE     0x00010000 // RSTDONE: Set to indicate that the IC finished its power-up sequence after a reset or after changing between PSM3 operating mode to PSM0
#define ADE_STATUS1_BITS_ZXIC        0x00008000 // ZXIC: When this bit is set to 1, it indicates a zero crossing is detected on Phase C current
#define ADE_STATUS1_BITS_ZXIB        0x00004000 // ZXIB: When this bit is set to 1, it indicates a zero crossing is detected on Phase B current
#define ADE_STATUS1_BITS_ZXIA        0x00002000 // ZXIA: When this bit is set to 1, it indicates a zero crossing is detected on Phase A current
#define ADE_STATUS1_BITS_ZXCOMB      0x00001000 // ZXCOMB: When this bit is set, it indicates a zero crossing is detected on the combined signal from VA, VB, and VC
#define ADE_STATUS1_BITS_ZXVC        0x00000800 // ZXVC: When this bit is set, it indicates a zero crossing is detected on the Phase C voltage channel
#define ADE_STATUS1_BITS_ZXVB        0x00000400 // ZXVB: When this bit is set, it indicates a zero crossing is detected on the Phase B voltage channel
#define ADE_STATUS1_BITS_ZXVA        0x00000200 // ZXVA: When this bit is set, it indicates a zero crossing is detected on the Phase A voltage channel
#define ADE_STATUS1_BITS_ZXTOVC      0x00000100 // ZXTOVC: This bit is set to indicate a zero-crossing timeout on Phase C
#define ADE_STATUS1_BITS_ZXTOVB      0x00000080 // ZXTOVB: This bit is set to indicate a zero-crossing timeout on Phase B
#define ADE_STATUS1_BITS_ZXTOVA      0x00000040 // ZXTOVA: This bit is set to indicate a zero-crossing timeout on Phase A
#define ADE_STATUS1_BITS_VAFNOLOAD   0x00000020 // VAFNOLOAD: Set when one or more phase fundamental apparent energy enters or exits the no load condition
#define ADE_STATUS1_BITS_RFNOLOAD    0x00000010 // RFNOLOAD: Set when one or more phase fundamental reactive energy enters or exits the no load condition
#define ADE_STATUS1_BITS_AFNOLOAD    0x00000008 // AFNOLOAD: Set when one or more phase fundamental active energy enters or exits the no load condition
#define ADE_STATUS1_BITS_VANLOAD     0x00000004 // VANLOAD: Set when one or more phase total apparent energy enters or exits the no load condition
#define ADE_STATUS1_BITS_RNLOAD      0x00000002 // RNLOAD: Set when one or more phase total reactive energy enters or exits the no load condition
#define ADE_STATUS1_BITS_ANLOAD      0x00000001 // ANLOAD: Set when one or more phase total active energy enters or exits the no load condition

// Estructura para el registro STATUS1
typedef union {
  struct {
    unsigned ANLOAD : 1;        // Total active energy enters or exits no load condition
    unsigned RNLOAD : 1;        // Total reactive energy enters or exits no load condition
    unsigned VANLOAD : 1;       // Total apparent energy enters or exits no load condition
    unsigned AFNOLOAD : 1;      // Fundamental active energy enters or exits no load condition
    unsigned RFNOLOAD : 1;      // Fundamental reactive energy enters or exits no load condition
    unsigned VAFNOLOAD : 1;     // Fundamental apparent energy enters or exits no load condition
    unsigned ZXTOVA : 1;        // Zero-crossing timeout on Phase A voltage missing
    unsigned ZXTOVB : 1;        // Zero-crossing timeout on Phase B voltage missing
    unsigned ZXTOVC : 1;        // Zero-crossing timeout on Phase C voltage missing
    unsigned ZXVA : 1;          // Zero crossing detected on Phase A voltage channel
    unsigned ZXVB : 1;          // Zero crossing detected on Phase B voltage channel
    unsigned ZXVC : 1;          // Zero crossing detected on Phase C voltage channel
    unsigned ZXCOMB : 1;        // Zero crossing detected on combined signal from VA, VB, and VC
    unsigned ZXIA : 1;          // Zero crossing detected on Phase A current
    unsigned ZXIB : 1;          // Zero crossing detected on Phase B current
    unsigned ZXIC : 1;          // Zero crossing detected on Phase C current
    unsigned RSTDONE : 1;       // IC finished power-up sequence after reset or changing modes
    unsigned OI : 1;            // Overcurrent event occurred on one of the phases
    unsigned SEQERR : 1;        // Phase sequence error on Phase voltage zero crossings
    unsigned RESERVED : 1;      // Reserved
    unsigned SWELLA : 1;        // Phase A voltage entered or exited a swell condition
    unsigned SWELLB : 1;        // Phase B voltage entered or exited a swell condition
    unsigned SWELLC : 1;        // Phase C voltage entered or exited a swell condition
    unsigned DIPA : 1;          // Phase A voltage entered or exited a dip condition
    unsigned DIPB : 1;          // Phase B voltage entered or exited a dip condition
    unsigned DIPC : 1;          // Phase C voltage entered or exited a dip condition
    unsigned CRC_CHG : 1;       // Configuration register CRC change value detected
    unsigned CRC_DONE : 1;      // Configuration register CRC calculation complete
    unsigned ERROR0 : 1;        // Error indicating nonmaskable interrupt
    unsigned ERROR1 : 1;        // Error indicating nonmaskable interrupt
    unsigned ERROR2 : 1;        // Error detected and corrected
    unsigned ERROR3 : 1;        // Error indicating nonmaskable interrupt
  };
  uint32_t raw;
} ADE_STATUS1_t;


// Defines for EVENT_STATUS register
#define ADE_EVENT_STATUS_BITS_DREADY         0x00010000 // DREADY: Indicates new waveform samples are ready
#define ADE_EVENT_STATUS_BITS_VAFNOLOAD      0x00008000 // VAFNOLOAD: Set when the fundamental apparent energy accumulations in all phases are out of no load
#define ADE_EVENT_STATUS_BITS_RFNOLOAD       0x00004000 // RFNOLOAD: Set when the fundamental reactive energy accumulations in all phases are out of no load
#define ADE_EVENT_STATUS_BITS_AFNOLOAD       0x00002000 // AFNOLOAD: Set when the fundamental active energy accumulations in all phases are out of no load
#define ADE_EVENT_STATUS_BITS_VANLOAD        0x00001000 // VANLOAD: Set when the total apparent energy accumulations in all phases are out of no load
#define ADE_EVENT_STATUS_BITS_RNLOAD         0x00000800 // RNLOAD: Set when the total reactive energy accumulations in all phases are out of no load
#define ADE_EVENT_STATUS_BITS_ANLOAD         0x00000400 // ANLOAD: Set when the total active energy accumulations in all phases are out of no load
#define ADE_EVENT_STATUS_BITS_REVPSUM4       0x00000200 // REVPSUM4: Indicates the sign of the last CF4 pulse
#define ADE_EVENT_STATUS_BITS_REVPSUM3       0x00000100 // REVPSUM3: Indicates the sign of the last CF3 pulse
#define ADE_EVENT_STATUS_BITS_REVPSUM2       0x00000080 // REVPSUM2: Indicates the sign of the last CF2 pulse
#define ADE_EVENT_STATUS_BITS_REVPSUM1       0x00000040 // REVPSUM1: Indicates the sign of the last CF1 pulse
#define ADE_EVENT_STATUS_BITS_SWELLC         0x00000020 // SWELLC: Indicates if Phase C voltage is in swell condition
#define ADE_EVENT_STATUS_BITS_SWELLB         0x00000010 // SWELLB: Indicates if Phase B voltage is in swell condition
#define ADE_EVENT_STATUS_BITS_SWELLA         0x00000008 // SWELLA: Indicates if Phase A voltage is in swell condition
#define ADE_EVENT_STATUS_BITS_DIPC           0x00000004 // DIPC: Indicates if Phase C voltage is in dip condition
#define ADE_EVENT_STATUS_BITS_DIPB           0x00000002 // DIPB: Indicates if Phase B voltage is in dip condition
#define ADE_EVENT_STATUS_BITS_DIPA           0x00000001 // DIPA: Indicates if Phase B voltage is in dip condition

// Struct for EVENT_STATUS register
typedef union {
  struct {
    unsigned DIPA : 1;           // DIPA: Indicates if Phase A voltage is in dip condition
    unsigned DIPB : 1;           // DIPB: Indicates if Phase B voltage is in dip condition
    unsigned DIPC : 1;           // DIPC: Indicates if Phase C voltage is in dip condition
    unsigned SWELLA : 1;         // SWELLA: Indicates if Phase A voltage is in swell condition
    unsigned SWELLB : 1;         // SWELLB: Indicates if Phase B voltage is in swell condition
    unsigned SWELLC : 1;         // SWELLC: Indicates if Phase C voltage is in swell condition
    unsigned REVPSUM1 : 1;       // REVPSUM1: Indicates the sign of the last CF1 pulse
    unsigned REVPSUM2 : 1;       // REVPSUM2: Indicates the sign of the last CF2 pulse
    unsigned REVPSUM3 : 1;       // REVPSUM3: Indicates the sign of the last CF3 pulse
    unsigned REVPSUM4 : 1;       // REVPSUM4: Indicates the sign of the last CF4 pulse
    unsigned ANLOAD : 1;         // ANLOAD: Set when the total active energy accumulations in all phases are out of no load
    unsigned RNLOAD : 1;         // RNLOAD: Set when the total reactive energy accumulations in all phases are out of no load
    unsigned VANLOAD : 1;        // VANLOAD: Set when the total apparent energy accumulations in all phases are out of no load
    unsigned AFNOLOAD : 1;       // AFNOLOAD: Set when the fundamental active energy accumulations in all phases are out of no load
    unsigned RFNOLOAD : 1;       // RFNOLOAD: Set when the fundamental reactive energy accumulations in all phases are out of no load
    unsigned VAFNOLOAD : 1;      // VAFNOLOAD: Set when the fundamental apparent energy accumulations in all phases are out of no load
    unsigned DREADY : 1;         // DREADY: Indicates new waveform samples are ready
    unsigned RESERVED : 15;     // RESERVED: Reserved bits
  };
  uint32_t raw;
} ADE_EVENT_STATUS_t;


// Defines for MASK0 register
#define ADE_MASK0_BITS_TEMP_RDY_MASK         0x02000000 /* Set this bit to enable an interrupt when a new temperature measurement is available */
#define ADE_MASK0_BITS_MISMTCH               0x01000000 /* Set this bit to enable an interrupt when there is a change in the relationship between ISUMRMS and ISUMLVL */
#define ADE_MASK0_BITS_COH_WFB_FULL          0x00800000 /* Set this bit to enable an interrupt when the waveform buffer is full with resampled data */
#define ADE_MASK0_BITS_WFB_TRIG              0x00400000 /* Set this bit to enable an interrupt when one of the events configured in WFB_TRIG_CFG occurs */
#define ADE_MASK0_BITS_THD_PF_RDY            0x00200000 /* Set this bit to enable an interrupt when the THD and power factor measurements are updated, every 1.024 sec */
#define ADE_MASK0_BITS_RMS1012RDY            0x00100000 /* Set this bit to enable an interrupt when the 10 cycle rms/12 cycle rms values are updated */
#define ADE_MASK0_BITS_RMSONERDY             0x00080000 /* Set this bit to enable an interrupt when the fast rms½ values are updated */
#define ADE_MASK0_BITS_PWRRDY                0x00040000 /* Set this bit to enable an interrupt when the power values in the xWATT_ACC, xVA_ACC, xVAR_ACC, xFWATT_ACC, xFVA_ACC, and xFVAR_ACC registers update, after PWR_TIME 8 kSPS samples */
#define ADE_MASK0_BITS_PAGE_FULL             0x00020000 /* Set this bit to enable an interrupt when a page enabled in the WFB_PG_IRQEN register is filled */
#define ADE_MASK0_BITS_WFB_TRIG_IRQ          0x00010000 /* Set this bit to enable an interrupt when the waveform buffer has stopped filling after an event configured in WFB_TRIG_CFG occurs */
#define ADE_MASK0_BITS_DREADY                0x00008000 /* Set this bit to enable an interrupt when new waveform samples are ready */
#define ADE_MASK0_BITS_CF4                   0x00004000 /* Set this bit to enable an interrupt when the CF4 pulse is issued */
#define ADE_MASK0_BITS_CF3                   0x00002000 /* Set this bit to enable an interrupt when the CF3 pulse is issued */
#define ADE_MASK0_BITS_CF2                   0x00001000 /* Set this bit to enable an interrupt when the CF2 pulse is issued */
#define ADE_MASK0_BITS_CF1                   0x00000800 /* Set this bit to enable an interrupt when the CF1 pulse is issued */
#define ADE_MASK0_BITS_REVPSUM4              0x00000400 /* Set this bit to enable an interrupt when the CF4 polarity changed sign */
#define ADE_MASK0_BITS_REVPSUM3              0x00000200 /* Set this bit to enable an interrupt when the CF3 polarity changed sign */
#define ADE_MASK0_BITS_REVPSUM2              0x00000100 /* Set this bit to enable an interrupt when the CF2 polarity changed sign */
#define ADE_MASK0_BITS_REVPSUM1              0x00000080 /* Set this bit to enable an interrupt when the CF1 polarity changed sign */
#define ADE_MASK0_BITS_REVRPC                0x00000040 /* Set this bit to enable an interrupt when the Phase C total or fundamental reactive power has changed sign */
#define ADE_MASK0_BITS_REVRPB                0x00000020 /* Set this bit to enable an interrupt when the Phase C total or fundamental reactive power has changed sign */
#define ADE_MASK0_BITS_REVRPA                0x00000010 /* Set this bit to enable an interrupt when the Phase A total or fundamental reactive power has changed sign */
#define ADE_MASK0_BITS_REVAPC                0x00000008 /* Set this bit to enable an interrupt when the Phase C total or fundamental active power has changed sign */
#define ADE_MASK0_BITS_REVAPB                0x00000004 /* Set this bit to enable an interrupt when the Phase B total or fundamental active power has changed sign */
#define ADE_MASK0_BITS_REVAPA                0x00000002 /* Set this bit to enable an interrupt when the Phase A total or fundamental active power has changed sign */
#define ADE_MASK0_BITS_EGYRDY                0x00000001 /* Set this bit to enable an interrupt when the power values in the registers update */

// Struct for MASK0 register
typedef union {
  struct {
    unsigned EGYRDY : 1;  // Bit 0: Enable interrupt when power values in certain registers update
    unsigned REVAPA : 1;  // Bit 1: Enable interrupt when Phase A total or fundamental active power changed sign
    unsigned REVAPB : 1;  // Bit 2: Enable interrupt when Phase B total or fundamental active power changed sign
    unsigned REVAPC : 1;  // Bit 3: Enable interrupt when Phase C total or fundamental active power changed sign
    unsigned REVRPA : 1;  // Bit 4: Enable interrupt when Phase A total or fundamental reactive power changed sign
    unsigned REVRPB : 1;  // Bit 5: Enable interrupt when Phase B total or fundamental reactive power changed sign
    unsigned REVRPC : 1;  // Bit 6: Enable interrupt when Phase C total or fundamental reactive power changed sign
    unsigned REVPSUM1 : 1;  // Bit 7: Enable interrupt when CF1 polarity changed sign
    unsigned REVPSUM2 : 1;  // Bit 8: Enable interrupt when CF2 polarity changed sign
    unsigned REVPSUM3 : 1;  // Bit 9: Enable interrupt when CF3 polarity changed sign
    unsigned REVPSUM4 : 1;  // Bit 10: Enable interrupt when CF4 polarity changed sign
    unsigned CF1 : 1;  // Bit 11: Enable interrupt when CF1 pulse is issued
    unsigned CF2 : 1;  // Bit 12: Enable interrupt when CF2 pulse is issued
    unsigned CF3 : 1;  // Bit 13: Enable interrupt when CF3 pulse is issued
    unsigned CF4 : 1;  // Bit 14: Enable interrupt when CF4 pulse is issued
    unsigned DREADY : 1;  // Bit 15: Enable interrupt when new waveform samples are ready
    unsigned WFB_TRIG_IRQ : 1;  // Bit 16: Enable interrupt when waveform buffer has stopped filling after an event in WFB_TRIG_CFG
    unsigned PAGE_FULL : 1;  // Bit 17: Enable interrupt when a page enabled in the WFB_PG_IRQEN register is filled
    unsigned PWRRDY : 1;  // Bit 18: Enable interrupt when the power values update
    unsigned RMSONERDY : 1;  // Bit 19: Enable interrupt when the fast rms½ values are updated
    unsigned RMS1012RDY : 1;  // Bit 20: Enable interrupt when the 10 cycle rms/12 cycle rms values are updated
    unsigned THD_PF_RDY : 1;  // Bit 21: Enable interrupt when the THD and power factor measurements are updated
    unsigned WFB_TRIG : 1;  // Bit 22: Enable interrupt when one of the events configured in WFB_TRIG_CFG occurs
    unsigned COH_WFB_FULL : 1;  // Bit 23: Enable interrupt when the waveform buffer is full with resampled data
    unsigned MISMTCH : 1;  // Bit 24: Enable interrupt when there is a change in the relationship between ISUMRMS and ISUMLVL
    unsigned TEMP_RDY_MASK : 1;  // Bit 25: Enable interrupt when new temperature measurement is available
    unsigned RESERVED : 7;  // Bits [31:26]: Reserved
  };
  uint32_t raw;
} ADE_MASK0_t;


// Defines for MASK1 register
#define ADE_MASK1_BITS_ERROR3                0x80000000 /* Set this bit to enable an interrupt if ERROR3 occurs. */
#define ADE_MASK1_BITS_ERROR2                0x40000000 /* Set this bit to enable an interrupt if ERROR2 occurs. */
#define ADE_MASK1_BITS_ERROR1                0x20000000 /* This interrupt is not maskable. */
#define ADE_MASK1_BITS_ERROR0                0x10000000 /* This interrupt is not maskable. */
#define ADE_MASK1_BITS_CRC_DONE              0x08000000 /* Set this bit to enable an interrupt when the configuration register CRC calculation is complete, after initiated by writing the FORCE_CRC_UPDATE bit in the CRC_FORCE register. */
#define ADE_MASK1_BITS_CRC_CHG               0x04000000 /* Set this bit to enable an interrupt if any of the registers monitored by the configuration register CRC change value. */
#define ADE_MASK1_BITS_DIPC                  0x02000000 /* Set this bit to enable an interrupt when the Phase C voltage enters a dip condition. */
#define ADE_MASK1_BITS_DIPB                  0x01000000 /* Set this bit to enable an interrupt when the Phase B voltage enters a dip condition. */
#define ADE_MASK1_BITS_DIPA                  0x00800000 /* Set this bit to enable an interrupt when the Phase A voltage enters a dip condition. */
#define ADE_MASK1_BITS_SWELLC                0x00400000 /* Set this bit to enable an interrupt when the Phase C voltage enters a swell condition. */
#define ADE_MASK1_BITS_SWELLB                0x00200000 /* Set this bit to enable an interrupt when the Phase B voltage enters a swell condition. */
#define ADE_MASK1_BITS_SWELLA                0x00100000 /* Set this bit to enable an interrupt when the Phase A voltage enters a swell condition. */
#define ADE_MASK1_BITS_RESERVED              0x00080000 /* Reserved. */
#define ADE_MASK1_BITS_SEQERR                0x00040000 /* Set this bit to enable an interrupt when on a phase sequence error on the phase voltage zero crossings. */
#define ADE_MASK1_BITS_OI                    0x00020000 /* Set this bit to enable an interrupt when one of the currents enabled in the OC_EN bits in the CONFIG3 register enters an overcurrent condition. */
#define ADE_MASK1_BITS_ZXIC                  0x00008000 /* Set this bit to enable an interrupt when a zero crossing is detected on the Phase C current channel. */
#define ADE_MASK1_BITS_ZXIB                  0x00004000 /* Set this bit to enable an interrupt when a zero crossing is detected on the Phase B current channel. */
#define ADE_MASK1_BITS_ZXIA                  0x00002000 /* Set this bit to enable an interrupt when a zero crossing is detected on the Phase A current channel. */
#define ADE_MASK1_BITS_ZXCOMB                0x00001000 /* Set this bit to enable an interrupt when a zero crossing is detected on the combined signal from VA, VB, and VC. */
#define ADE_MASK1_BITS_ZXVC                  0x00000800 /* Set this bit to enable an interrupt when a zero crossing is detected on the Phase C voltage channel. */
#define ADE_MASK1_BITS_ZXVB                  0x00000400 /* Set this bit to enable an interrupt when a zero crossing is detected on the Phase B voltage channel. */
#define ADE_MASK1_BITS_ZXVA                  0x00000200 /* Set this bit to enable an interrupt when a zero crossing is detected on the Phase A voltage channel. */
#define ADE_MASK1_BITS_ZXTOVC                0x00000100 /* Set this bit to enable an interrupt when there is a zero-crossing timeout on Phase C. This means that a zero crossing on the Phase C voltage is missing. */
#define ADE_MASK1_BITS_ZXTOVB                0x00000080 /* Set this bit to enable an interrupt when there is a zero-crossing timeout on Phase B. This means that a zero crossing on the Phase B voltage is missing. */
#define ADE_MASK1_BITS_ZXTOVA                0x00000040 /* Set this bit to enable an interrupt when there is a zero-crossing timeout on Phase A. This means that a zero crossing on the Phase A voltage is missing. */
#define ADE_MASK1_BITS_VAFNOLOAD             0x00000020 /* Set this bit to enable an interrupt when one or more phase fundamental apparent energy enters or exits the no load condition. */
#define ADE_MASK1_BITS_RFNOLOAD              0x00000010 /* Set this bit to enable an interrupt when one or more phase total reactive energy enters or exits the no load condition. */
#define ADE_MASK1_BITS_AFNOLOAD              0x00000008 /* Set this bit to enable an interrupt when one or more phase fundamental active energy enters or exits the no load condition. */
#define ADE_MASK1_BITS_VANLOAD               0x00000004 /* Set this bit to enable an interrupt when one or more phase total apparent energy enters or exits the no load condition. */
#define ADE_MASK1_BITS_RNLOAD                0x00000002 /* Set this bit to enable an interrupt when one or more phase total reactive energy enters or exits the no load condition. */
#define ADE_MASK1_BITS_ANLOAD                0x00000001 /* Set this bit to enable an interrupt when one or more phase total active energy enters or exits the no load condition. */

// Definición de la estructura ADE_MASK1_t
typedef union {
  struct {
    unsigned ANLOAD : 1;            // ANLOAD: Phase total active energy no load condition interrupt enable
    unsigned RNLOAD : 1;            // RNLOAD: Phase total reactive energy no load condition interrupt enable
    unsigned VANLOAD : 1;           // VANLOAD: Phase total apparent energy no load condition interrupt enable
    unsigned AFNOLOAD : 1;          // AFNOLOAD: Phase fundamental active energy no load condition interrupt enable
    unsigned RFNOLOAD : 1;          // RFNOLOAD: Phase total reactive energy no load condition interrupt enable
    unsigned VAFNOLOAD : 1;         // VAFNOLOAD: Phase fundamental apparent energy no load condition interrupt enable
    unsigned ZXTOVA : 1;            // ZXTOVA: Zero crossing timeout on Phase A interrupt enable
    unsigned ZXTOVB : 1;            // ZXTOVB: Zero crossing timeout on Phase B interrupt enable
    unsigned ZXTOVC : 1;            // ZXTOVC: Zero crossing timeout on Phase C interrupt enable
    unsigned ZXVA : 1;              // ZXVA: Zero crossing on Phase A voltage channel interrupt enable
    unsigned ZXVB : 1;              // ZXVB: Zero crossing on Phase B voltage channel interrupt enable
    unsigned ZXVC : 1;              // ZXVC: Zero crossing on Phase C voltage channel interrupt enable
    unsigned ZXCOMB : 1;            // ZXCOMB: Zero crossing on combined signal interrupt enable
    unsigned ZXIA : 1;              // ZXIA: Zero crossing on Phase A current channel interrupt enable
    unsigned ZXIB : 1;              // ZXIB: Zero crossing on Phase B current channel interrupt enable
    unsigned ZXIC : 1;              // ZXIC: Zero crossing on Phase C current channel interrupt enable
    unsigned RESERVED2 : 1;         // RESERVED: Reserved bits
    unsigned OI : 1;                // OI: Overcurrent condition interrupt enable
    unsigned SEQERR : 1;            // SEQERR: Phase sequence error interrupt enable
    unsigned RESERVED1 : 1;         // RESERVED: Reserved bits
    unsigned SWELLA : 1;            // SWELLA: Phase A voltage swell condition interrupt enable
    unsigned SWELLB : 1;            // SWELLB: Phase B voltage swell condition interrupt enable
    unsigned SWELLC : 1;            // SWELLC: Phase C voltage swell condition interrupt enable
    unsigned DIPA : 1;              // DIPA: Phase A voltage dip condition interrupt enable
    unsigned DIPB : 1;              // DIPB: Phase B voltage dip condition interrupt enable
    unsigned DIPC : 1;              // DIPC: Phase C voltage dip condition interrupt enable
    unsigned CRC_CHG : 1;           // CRC_CHG: CRC change interrupt enable
    unsigned CRC_DONE : 1;          // CRC_DONE: CRC calculation complete interrupt enable
    unsigned ERROR0 : 1;            // ERROR0: Error 0 interrupt (not maskable)
    unsigned ERROR1 : 1;            // ERROR1: Error 1 interrupt (not maskable)
    unsigned ERROR2 : 1;            // ERROR2: Error 2 interrupt enable
    unsigned ERROR3 : 1;            // ERROR3: Error 3 interrupt enable
  };
  uint32_t raw;
} ADE_MASK1_t;



// Defines for EVENT_MASK register
#define ADE_EVENT_MASK_BITS_DREADY           0x00010000 /* Set this bit to enable the EVENT pin to go low when new waveform samples are ready. */
#define ADE_EVENT_MASK_BITS_VAFNOLOAD        0x00008000 /* Set this bit to enable the EVENT pin to go low when one or more phases of fundamental apparent energy accumulation goes into no load. */
#define ADE_EVENT_MASK_BITS_RFNOLOAD         0x00004000 /* Set this bit to enable the EVENT pin to go low when one or more phases of fundamental reactive energy accumulation goes into no load. */
#define ADE_EVENT_MASK_BITS_AFNOLOAD         0x00002000 /* Set this bit to enable the EVENT pin to go low when one or more phases of fundamental active energy accumulation goes into no load. */
#define ADE_EVENT_MASK_BITS_VANLOAD          0x00001000 /* Set this bit to enable the EVENT pin to go low when one or more phases of total apparent energy accumulation goes into no load. */
#define ADE_EVENT_MASK_BITS_RNLOAD           0x00000800 /* Set this bit to enable the EVENT pin to go low when one or more phases of total reactive energy accumulation goes into no load. */
#define ADE_EVENT_MASK_BITS_ANLOAD           0x00000400 /* Set this bit to enable the EVENT pin to go low when one or more phases of total active energy accumulation goes into no load. */
#define ADE_EVENT_MASK_BITS_REVPSUM4         0x00000200 /* Set this bit to enable the EVENT pin to go low to indicate if the last CF4 pulse was from negative energy. */
#define ADE_EVENT_MASK_BITS_REVPSUM3         0x00000100 /* Set this bit to enable the EVENT pin to go low to indicate if the last CF3 pulse was from negative energy. */
#define ADE_EVENT_MASK_BITS_REVPSUM2         0x00000080 /* Set this bit to enable the EVENT pin to go low to indicate if the last CF2 pulse was from negative energy. */
#define ADE_EVENT_MASK_BITS_REVPSUM1         0x00000040 /* Set this bit to enable the EVENT pin to go low to indicate if the last CF1 pulse was from negative energy. */
#define ADE_EVENT_MASK_BITS_SWELLCEN         0x00000020 /* Set this bit to enable the EVENT pin to go low to indicate that the Phase C voltage is in a swell condition. */
#define ADE_EVENT_MASK_BITS_SWELLBEN         0x00000010 /* Set this bit to enable the EVENT pin to go low to indicate that the Phase B voltage is in a swell condition. */
#define ADE_EVENT_MASK_BITS_SWELLAEN         0x00000008 /* Set this bit to enable the EVENT pin to go low to indicate that the Phase A voltage is in a swell condition. */
#define ADE_EVENT_MASK_BITS_DIPCEN           0x00000004 /* Set this bit to enable the EVENT pin to go low to indicate that the Phase C voltage is in a dip condition. */
#define ADE_EVENT_MASK_BITS_DIPBEN           0x00000002 /* Set this bit to enable the EVENT pin to go low to indicate that the Phase B voltage is in a dip condition. */
#define ADE_EVENT_MASK_BITS_DIPAEN           0x00000001 /* Set this bit to enable the EVENT pin to go low to indicate that the Phase A voltage is in a dip condition. */

// Definición de la estructura ADE_EVENT_MASK_t
typedef union {
  struct {
    unsigned DIPAEN : 1;        // DIPAEN: Phase A voltage dip condition interrupt enable
    unsigned DIPBEN : 1;        // DIPBEN: Phase B voltage dip condition interrupt enable
    unsigned DIPCEN : 1;        // DIPCEN: Phase C voltage dip condition interrupt enable
    unsigned SWELLAEN : 1;      // SWELLAEN: Phase A voltage swell condition interrupt enable
    unsigned SWELLBEN : 1;      // SWELLBEN: Phase B voltage swell condition interrupt enable
    unsigned SWELLCEN : 1;      // SWELLCEN: Phase C voltage swell condition interrupt enable
    unsigned REVPSUM1 : 1;     // REVPSUM1: Last CF1 pulse sign change interrupt enable
    unsigned REVPSUM2 : 1;     // REVPSUM2: Last CF2 pulse sign change interrupt enable
    unsigned REVPSUM3 : 1;     // REVPSUM3: Last CF3 pulse sign change interrupt enable
    unsigned REVPSUM4 : 1;     // REVPSUM4: Last CF4 pulse sign change interrupt enable
    unsigned ANLOAD : 1;        // ANLOAD: Phase total active energy no load condition interrupt enable
    unsigned RNLOAD : 1;        // RNLOAD: Phase total reactive energy no load condition interrupt enable
    unsigned VANLOAD : 1;       // VANLOAD: Phase total apparent energy no load condition interrupt enable
    unsigned AFNOLOAD : 1;      // AFNOLOAD: Phase fundamental active energy no load condition interrupt enable
    unsigned RFNOLOAD : 1;      // RFNOLOAD: Phase total reactive energy no load condition interrupt enable
    unsigned VAFNOLOAD : 1;     // VAFNOLOAD: Phase fundamental apparent energy no load condition interrupt enable
    unsigned THD_PF_RDY : 1;    // THD_PF_RDY: THD and power factor measurements update interrupt enable
    unsigned WFB_TRIG : 1;      // WFB_TRIG: Waveform buffer trigger event interrupt enable
    unsigned COH_WFB_FULL : 1;  // COH_WFB_FULL: Waveform buffer full with resampled data interrupt enable
    unsigned CRC_CHG : 1;       // CRC_CHG: CRC change interrupt enable
    unsigned CRC_DONE : 1;      // CRC_DONE: CRC calculation complete interrupt enable
    unsigned RESERVED : 11;     // RESERVED: Reserved bits
  };
  uint32_t raw;
} ADE_EVENT_MASK_t;


// Struct for OILVL register
typedef union {
  struct {
    unsigned OILVL_VAL : 24;    // Bits [23:0]: Over current detection threshold level
  };
  uint32_t raw;
} ADE_OILVL_t;

// Struct for OIA register
typedef union {
  struct {
    unsigned OI_VAL : 24;    // Bits [23:0]: Phase A overcurrent rms½ value
  };
  uint32_t raw;
} ADE_OIA_t;

// Struct for OIB register
typedef union {
  struct {
    unsigned OIB_VAL : 24;    // Bits [23:0]: Phase B overcurrent rms½ value
  };
  uint32_t raw;
} ADE_OIB_t;

// Struct for OIC register
typedef union {
  struct {
    unsigned OIC_VAL : 24;    // Bits [23:0]: Phase C overcurrent rms½ value
  };
  uint32_t raw;
} ADE_OIC_t;

// Struct for OIN register
typedef union {
  struct {
    unsigned OIN_VAL : 24;    // Bits [23:0]: Neutral current overcurrent rms½ value
  };
  uint32_t raw;
} ADE_OIN_t;

// Struct for VLEVEL register
typedef union {
  struct {
    unsigned VLEVEL_VAL : 24;    // Bits [23:0]: Register used in the algorithm that computes
    //             the fundamental active, reactive, and apparent
    //             powers, as well as the fundamental IRMS and VRMS values
  };
  uint32_t raw;
} ADE_VLEVEL_t;

// Struct for DIP_LVL register
typedef union {
  struct {
    unsigned DIPLVL : 24;   // Bits [23:0]: Voltage rms½ dip detection threshold level
  };
  uint32_t raw;
} ADE_DIP_LVL_t;

// Struct for DIPA register
typedef union {
  struct {
    unsigned DIPA_VAL : 24;   // Bits [23:0]: Phase A voltage rms½ value during a dip condition
  };
  uint32_t raw;
} ADE_DIPA_t;

// Struct for DIPB register
typedef union {
  struct {
    unsigned DIPB_VAL : 24;   // Bits [23:0]: Phase B voltage rms½ value during a dip condition
  };
  uint32_t raw;
} ADE_DIPB_t;

// Struct for DIPC register
typedef union {
  struct {
    unsigned DIPC_VAL : 24;   // Bits [23:0]: Phase C voltage rms½ value during a dip condition
  };
  uint32_t raw;
} ADE_DIPC_t;

// Struct for SWELL_LVL register
typedef union {
  struct {
    unsigned SWELLLVL : 24;   // Bits [23:0]: Voltage rms½ swell detection threshold level
  };
  uint32_t raw;
} ADE_SWELL_LVL_t;

// Struct for SWELLA register
typedef union {
  struct {
    unsigned SWELLA_VAL : 24;   // Bits [23:0]: Phase A voltage rms½ value during a swell condition
  };
  uint32_t raw;
} ADE_SWELLA_t;

// Struct for SWELLB register
typedef union {
  struct {
    unsigned SWELLB_VAL : 24;   // Bits [23:0]: Phase B voltage rms½ value during a swell condition
  };
  uint32_t raw;
} ADE_SWELLB_t;

// Struct for SWELLC register
typedef union {
  struct {
    unsigned SWELLC_VAL : 24;   // Bits [23:0]: Phase C voltage rms½ value during a swell condition
  };
  uint32_t raw;
} ADE_SWELLC_t;

// Struct for PHNOLOAD register
typedef union {
  struct {
    unsigned AWATTNL : 1;    // Bit 0: Phase A total active energy in no load
    unsigned AVARNL : 1;    // Bit 1: Phase A total reactive energy in no load
    unsigned AVANL : 1;    // Bit 2: Phase A total apparent energy in no load
    unsigned AFWATTNL : 1;    // Bit 3: Phase A fundamental active energy in no load
    unsigned AFVARNL : 1;    // Bit 4: Phase A fundamental reactive energy in no load
    unsigned AFVANL : 1;    // Bit 5: Phase A fundamental apparent energy in no load
    unsigned BWATTNL : 1;    // Bit 6: Phase B total active energy in no load
    unsigned BVARNL : 1;    // Bit 7: Phase B total reactive energy in no load
    unsigned BVANL : 1;    // Bit 8: Phase B total apparent energy in no load
    unsigned BFWATTNL : 1;    // Bit 9: Phase B fundamental active energy in no load
    unsigned BFVARNL : 1;    // Bit 10: Phase B fundamental reactive energy in no load
    unsigned BFVANL : 1;    // Bit 11: Phase B fundamental apparent energy in no load
    unsigned CWATTNL : 1;    // Bit 12: Phase C total active energy in no load
    unsigned CVARNL : 1;    // Bit 13: Phase C total reactive energy in no load
    unsigned CVANL : 1;    // Bit 14: Phase C total apparent energy in no load
    unsigned CFWATTNL : 1;    // Bit 15: Phase C fundamental active energy in no load
    unsigned CFVARNL : 1;    // Bit 16: Phase C fundamental reactive energy in no load
    unsigned CFVANL : 1;    // Bit 17: Phase C fundamental apparent energy in no load
    unsigned RESERVED : 14;   // Bits [18:31]: RESERVED
  };
  uint32_t raw;
} ADE_PHNOLOAD_t;

// Struct for ADC_REDIRECT register
typedef union {
  struct {
    unsigned IA_DIN : 3;      // Bits [0:2] IA_DIN: IA channel data selection
    unsigned IB_DIN : 3;      // Bits [3:5] IB_DIN: IB channel data selection
    unsigned IC_DIN : 3;      // Bits [6:8] IC_DIN: IC channel data selection
    unsigned IN_DIN : 3;      // Bits [9:11] IN_DIN: IN channel data selection
    unsigned VA_DIN : 3;      // Bits [12:14] VA_DIN: VA channel data selection
    unsigned VB_DIN : 3;      // Bits [15:17] VB_DIN: VB channel data selection
    unsigned VC_DIN : 3;      // Bits [18:20] VC_DIN: VC channel data selection
    // 000: IA ADC data
    // 001: IB ADC data
    // 010: IC ADC data
    // 011: IN ADC data
    // 100: VA ADC data
    // 101: VB ADC data
    // 110: VC ADC data
    // 111: VC ADC data
    unsigned RESERVED : 11;   // Bits [21:31] RESERVED: Reserved
  };
  uint32_t raw;
} ADE_ADC_REDIRECT_t;

// Struct for CF_LCFG register
typedef union {
  struct {
    unsigned CF_LTMR : 19;        // Bits [0:18] CF_LTMR: CFx pulse width configuration
    unsigned CF1_LT : 1;          // Bit 19 CF4_LT: CF4 pulse width configuration
    unsigned CF2_LT : 1;          // Bit 20 CF3_LT: CF3 pulse width configuration
    unsigned CF3_LT : 1;          // Bit 21 CF2_LT: CF2 pulse width configuration
    unsigned CF4_LT : 1;          // Bit 22 CF1_LT: CF1 pulse width configuration
  };
  uint32_t raw;
} ADE_CF_LCFG_t;

// Struct for PART_ID register
typedef union {
  struct {
    unsigned RESERVED : 20;     // Bits [0:19] RESERVED: Reserved
    unsigned ADE9000_ID : 1;      // Bit 20 ADE9000_ID: ADE9000 IC identification
    unsigned RESERVED_1 : 11;     // Bits [21:31] RESERVED: Reserved
  };
  uint32_t raw;
} ADE_PART_ID_t;

// Struct for TEMP_TRIM register
typedef union {
  struct {
    unsigned TEMP_GAIN : 16;  // Bits [15:0]: Gain of temperature sensor, calculated during the manufacturing process
    unsigned TEMP_OFFSET : 16;  // Bits [31:16]: Offset of temperature sensor, calculated during the manufacturing process
  };
  uint32_t raw;
} ADE_TEMP_TRIM_t;


// Bit Masks
#define ADE_CONFIG1_BITS_EXT_REF            0x8000  // Set this bit if using an external voltage reference.
#define ADE_CONFIG1_BITS_IRQ0_ON_IRQ1       0x1000  // Set this bit to combine all the interrupts onto a single interrupt pin, IRQ1, instead of using two pins, IRQ0 and IRQ1.
#define ADE_CONFIG1_BITS_BURST_EN           0x0800  // Set this bit to enable burst read functionality on the registers from Address 0x500 to Address 0x63C or Address 0x680 to Address 0x6BC.
#define ADE_CONFIG1_BITS_DIP_SWELL_IRQ_MODE 0x0400  // Set interrupt mode for dip/swell.
#define ADE_CONFIG1_BITS_PWR_SETTLE         0x0300  // These bits configure the time for the power and filter-based rms measurements to settle before starting the power, energy, and CF accumulations.
#define ADE_CONFIG1_BITS_RESERVED_1         0x00C0  // RESERVED: Reserved bits
#define ADE_CONFIG1_BITS_CF_ACC_CLR         0x0020  // Set this bit to clear the accumulation in the digital to frequency converter and the CFDEN counter.
#define ADE_CONFIG1_BITS_RESERVED_2         0x0010  // RESERVED: Reserved bits
#define ADE_CONFIG1_BITS_CF4_CFG            0x000C  // These bits select which function to output on the CF4 pin.
#define ADE_CONFIG1_BITS_CF3_CFG            0x0002  // This bit selects which function to output on the CF3 pin.
#define ADE_CONFIG1_BITS_SWRST              0x0001  // Set this bit to initiate a software reset.

// Definición de la estructura ADE_CONFIG1_t
typedef union {
  struct {
    unsigned SWRST : 1;         // Software reset initiation
    unsigned CF3_CFG : 1;       // CF3 pin function selection
    unsigned CF4_CFG : 2;       // CF4 pin function selection
    unsigned RESERVED_0 : 1;    // RESERVED: Reserved bit
    unsigned CF_ACC_CLR : 1;    // Clear accumulation in the digital to frequency converter and the CFDEN counter
    unsigned PWR_SETTLE : 2;    // Power and filter-based rms measurements settling time configuration
    unsigned DIP_SWELL_IRQ_MODE : 1; // Interrupt mode for dip/swell
    unsigned BURST_EN : 1;      // Burst read functionality enable
    unsigned IRQ0_ON_IRQ1 : 1;  // Combine all interrupts onto a single interrupt pin
    unsigned EXT_REF : 1;       // External voltage reference usage
    unsigned RESERVED_1 : 2;    // RESERVED: Reserved bits
    unsigned ININTEN : 1;       // Digital integrator in the neutral current channel enable
    unsigned VNOMC_EN : 1;      // Nominal phase voltage rms usage for Phase C total apparent power computation
    unsigned VNOMB_EN : 1;      // Nominal phase voltage rms usage for Phase B total apparent power computation
    unsigned VNOMA_EN : 1;      // Nominal phase voltage rms usage for Phase A total apparent power computation
    unsigned INRPLPF : 1;       // Disable low-pass filter in the neutral current datapath
    unsigned INRMSOS : 1;       // Neutral current channel offset calibration enable
    unsigned RESERVED_2 : 14;   // RESERVED: Reserved bits
  };
  uint16_t raw;
} ADE_CONFIG1_t;

// Struct for OISTATUS register
typedef union {
  struct {
    unsigned OIPHASE : 4;            // Bits [0:3] OIPHASE: Indicates whether each phase (A, B, C, N) is above OILVL. 
    // Bit 0: Phase A
    // Bit 1: Phase B
    // Bit 2: Phase C
    // Bit 3: Phase N
    unsigned RESERVED : 12;          // Bits [4:15] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_OISTATUS_t;


// Struct for CFMODE register
typedef union {
  struct {
    unsigned CF1SEL : 3;             // Bits [2:0] CF1SEL: Selects type of energy output on CF1 pin. See CF4SEL.
    unsigned CF2SEL : 3;             // Bits [5:3] CF2SEL: Selects type of energy output on CF2 pin. See CF4SEL.
    unsigned CF3SEL : 3;             // Bits [8:6] CF3SEL: Selects type of energy output on CF3 pin. See CF4SEL.
    unsigned CF4SEL : 3;             // Bits [11:9] CF4SEL: Type of energy output on the CF4 pin. Configure TERMSEL4 in the COMPMODE register to select which phases are included.
    unsigned CF1DIS : 1;             // Bit 12 CF1DIS: CF1 output disable. See CF4DIS.
    unsigned CF2DIS : 1;             // Bit 13 CF2DIS: CF2 output disable. See CF4DIS.
    unsigned CF3DIS : 1;             // Bit 14 CF3DIS: CF3 output disable. See CF4DIS.
    unsigned CF4DIS : 1;             // Bit 15 CF4DIS: CF4 output disable. Set this bit to disable the CF4 output and bring the pin high. Note that when this bit is set, the CFx bit in STATUS0 is not set when a CF pulse is accumulated in the digital to frequency converter.
  };
  uint16_t raw;
} ADE_CFMODE_t;

// Struct for COMPMODE register
typedef union {
  struct {
    unsigned TERMSEL1 : 3;           // Bits [2:0] TERMSEL1: Phases to include in CF1 pulse output. See TERMSEL4.
    unsigned TERMSEL2 : 3;           // Bits [5:3] TERMSEL2: Phases to include in CF2 pulse output. See TERMSEL4.
    unsigned TERMSEL3 : 3;           // Bits [8:6] TERMSEL3: Phases to include in CF3 pulse output. See TERMSEL4.
    unsigned TERMSEL4 : 3;           // Bits [11:9] TERMSEL4: Phases to include in CF4 pulse output. Set TERMSEL4, Bit 2 to 1 to include Phase C in the CF4 pulse output. Similarly, set TERMSEL4, Bit 1 to include Phase B, and TERMSEL4, Bit 0 for Phase A.
    unsigned RESERVED : 4;           // Bits [12:15] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_COMPMODE_t;



// Struct for ACCMODE register
typedef union {
  struct {
    unsigned WATTACC : 2;    // Bits [1:0] WATTACC: Active Power Accumulation Mode
    //   Same encoding as VARACC
    unsigned VARACC : 2;     // Bits [3:2] VARACC: Reactive Power Accumulation Mode
    //   00: Signed accumulation mode
    //   01: Absolute value accumulation mode
    //   10: Positive accumulation mode
    //   11: Negative accumulation mode
    unsigned VCONSEL : 3;    // Bits [6:4] VCONSEL: Voltage Configuration Selection
    //   000: 4-wire wye
    //   001: 3-wire delta (VB' = VA - VC)
    //   010: 4-wire wye, non-Blondel compliant (VB' = -VA - VC)
    //   011: 4-wire delta, non-Blondel compliant (VB' = -VA)
    //   100: 3-wire delta (VA' = VA - VB; VB' = VA - VC; VC' = VC - VB)
    unsigned ICONSEL : 1;    // Bit [7] ICONSEL: IB Calculation Selection
    //   0: Calculate IB normally
    //   1: Calculate IB from IA and IC: IB = -IA - IC
    unsigned SELFREQ : 1;    // Bit [8] SELFREQ: System Frequency Selection
    //   0: 50 Hz system
    //   1: 60 Hz system
    unsigned RESERVED : 7;   // Bits [15:9] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_ACCMODE_t;

// Struct for CONFIG3 register
typedef union {
  struct {
    unsigned RESERVED_0 : 2;   // Bits [1:0] RESERVED: Reserved
    unsigned PEAKSEL : 3;       // Bits [4:2] PEAKSEL: Peak Voltage and Current Monitoring Selection
    //   Bit 0: Enable Phase A peak detection
    //   Bit 1: Enable Phase B peak detection
    //   Bit 2: Enable Phase C peak detection
    unsigned RESERVED_1 : 7;    // Bits [11:5] RESERVED: Reserved
    unsigned OC_EN : 4;         // Bits [15:12] OC_EN: Overcurrent Detection Enable
    //   Bit 12: Enable overcurrent detection on Phase A
    //   Bit 13: Enable overcurrent detection on Phase B
    //   Bit 14: Enable overcurrent detection on Phase C
    //   Bit 15: Enable overcurrent detection on Neutral
  };
  uint16_t raw;
} ADE_CONFIG3_t;

// Struct for ZX_LP_SEL register
typedef union {
  struct {
    unsigned RESERVED_1 : 1;    // Bit 0 RESERVED: Reserved
    unsigned ZX_SEL : 2;        // Bits [2:1] ZX_SEL: Zero-Crossing Signal Selection
    //   00: ZXVA, Phase A voltage zero-crossing signal
    //   01: ZXVB, Phase B voltage zero-crossing signal
    //   10: ZXVC, Phase C voltage zero-crossing signal
    //   11: ZXCOMB, zero crossing on combined signal from VA, VB, and VC
    unsigned LP_SEL : 2;        // Bits [4:3] LP_SEL: Line Period Measurement Selection
    //   00: APERIOD, line period measurement from Phase A voltage
    //   01: BPERIOD, line period measurement from Phase B voltage
    //   10: CPERIOD, line period measurement from Phase C voltage
    //   11: COM_PERIOD, line period measurement on combined signal from VA, VB, and VC
    unsigned RESERVED_0 : 11;   // Bits [15:5] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_ZX_LP_SEL_t;


// Struct for PHSIGN register
typedef union {
  struct {
    unsigned AWSIGN : 1;        // Bit 0 AWSIGN: Phase A active power sign bit
    unsigned AVARSIGN : 1;      // Bit 1 AVARSIGN: Phase A reactive power sign bit
    unsigned BWSIGN : 1;        // Bit 2 BWSIGN: Phase B active power sign bit
    unsigned BVARSIGN : 1;      // Bit 3 BVARSIGN: Phase B reactive power sign bit
    unsigned CWSIGN : 1;        // Bit 4 CWSIGN: Phase C active power sign bit
    unsigned CVARSIGN : 1;      // Bit 5 CVARSIGN: Phase C reactive power sign bit
    unsigned SUM1SIGN : 1;      // Bit 6 SUM1SIGN: Sign of the sum of the powers included in the CF1 datapath
    unsigned SUM2SIGN : 1;      // Bit 7 SUM2SIGN: Sign of the sum of the powers included in the CF2 datapath
    unsigned SUM3SIGN : 1;      // Bit 8 SUM3SIGN: Sign of the sum of the powers included in the CF3 datapath
    unsigned SUM4SIGN : 1;      // Bit 9 SUM4SIGN: Sign of the sum of the powers included in the CF4 datapath
    unsigned RESERVED_0 : 6;    // Bits [15:10] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_PHSIGN_t;


// Struct for WFB_CFG register
typedef union {
  struct {
    unsigned BURST_CHAN : 4;    // Bits [3:0] BURST_CHAN: Selects data to read out of the waveform buffer
    //   0000: All channels
    //   0001: IA and VA
    //   0010: IB and VB
    //   0011: IC and VC
    //   1000: IA
    //   1001: VA
    //   1010: IB
    //   1011: VB
    //   1100: IC
    //   1101: VC
    //   1110: IN (if WF_IN_EN = 1)
    //   1111: Single address read (SPI burst read mode disabled)
    unsigned WF_CAP_EN : 1;     // Bit 4 WF_CAP_EN: Waveform capture enable
    //   0: Waveform capture disabled
    //   1: Waveform capture started
    unsigned WF_CAP_SEL : 1;    // Bit 5 WF_CAP_SEL: Waveform capture selection
    //   0: Resampled data
    //   1: Fixed data rate data
    unsigned WF_MODE : 2;       // Bits [7:6] WF_MODE: Fixed data rate waveforms filling and trigger based modes
    //   00: Stop when waveform buffer is full
    //   01: Continuous fill—stop only on enabled trigger events
    //   10: Continuous filling—center capture around enabled trigger events
    //   11: Continuous fill—save event address of enabled trigger events
    unsigned WF_SRC : 2;        // Bits [9:8] WF_SRC: Waveform buffer source and DREADY selection
    //   00: Sinc4 output at 32 kSPS
    //   01: Reserved
    //   10: Sinc4 + IIR LPF output at 8 kSPS
    //   11: Current and voltage channel waveform samples processed by the DSP (xI_PCF, xV_PCF) at 8 kSPS
    unsigned RESERVED_1 : 2;    // Bits [11:10] RESERVED: Reserved
    unsigned WF_IN_EN : 1;      // Bit 12 WF_IN_EN: Waveform buffer IN waveform samples enable
    //   0: IN waveform samples not read out through SPI
    //   1: IN waveform samples read out through SPI
  };
  uint16_t raw;
} ADE_WFB_CFG_t;


// Struct for WFB_TRG_CFG register
typedef union {
  struct {
    unsigned DIP : 1;        // Bit 0 DIP: Dip event in any phase
    //   0: No event
    //   1: Dip event in any phase
    unsigned SWELL : 1;      // Bit 1 SWELL: Swell event in any phase
    //   0: No event
    //   1: Swell event in any phase
    unsigned OI : 1;         // Bit 2 OI: Overcurrent event in any phase
    //   0: No event
    //   1: Overcurrent event in any phase
    unsigned ZXIA : 1;       // Bit 3 ZXIA: Phase A current zero crossing
    //   0: No zero crossing
    //   1: Phase A current zero crossing
    unsigned ZXIB : 1;       // Bit 4 ZXIB: Phase B current zero crossing
    //   0: No zero crossing
    //   1: Phase B current zero crossing
    unsigned ZXIC : 1;       // Bit 5 ZXIC: Phase C current zero crossing
    //   0: No zero crossing
    //   1: Phase C current zero crossing
    unsigned ZXVA : 1;       // Bit 6 ZXVA: Phase A voltage zero crossing
    //   0: No zero crossing
    //   1: Phase A voltage zero crossing
    unsigned ZXVB : 1;       // Bit 7 ZXVB: Phase B voltage zero crossing
    //   0: No zero crossing
    //   1: Phase B voltage zero crossing
    unsigned ZXVC : 1;       // Bit 8 ZXVC: Phase C voltage zero crossing
    //   0: No zero crossing
    //   1: Phase C voltage zero crossing
    unsigned ZXCOMB : 1;     // Bit 9 ZXCOMB: Zero crossing on combined signal from VA, VB, and VC
    //   0: No zero crossing
    //   1: Zero crossing on combined signal
    unsigned TRIG_FORCE : 1;// Bit 10 TRIG_FORCE: Set this bit to trigger an event to stop the waveform buffer filling
    //   0: No event forced
    //   1: Trigger event to stop waveform buffer filling
    unsigned RESERVED : 5;  // Bits [15:11] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_WFB_TRG_CFG_t;


// Struct for WFB_TRG_STAT register
typedef union {
  struct {
    unsigned WFB_TRIG_ADDR : 11;   // Bits [0:10] WFB_TRIG_ADDR: Address of the last sample put into the waveform buffer after a trigger event
    unsigned RESERVED : 1;         // Bit 11 RESERVED: Reserved
    unsigned WFB_LAST_PAGE : 4;    // Bits [12:15] WFB_LAST_PAGE: Indicate which page of the waveform buffer was filled last when filling with fixed rate data samples
  };
  uint16_t raw;
} ADE_WFB_TRG_STAT_t;



// Struct for CONFIG2 register
typedef union {
  struct {
    unsigned RESERVED_0 : 9;    // Bits [0:8] RESERVED: Reserved
    unsigned HPF_CRN : 3;       // Bits [9:11] HPF_CRN: High-pass filter corner frequency
    //   See datasheet for specific values
    unsigned UPERIOD_SEL : 1;   // Bit 12 UPERIOD_SEL: Use user-configured line period
    //   0: Use line period from ZX_LP_SEL register
    //   1: Use user-configured line period
    unsigned RESERVED_1 : 3;    // Bits [13:15] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_CONFIG2_t;


// Struct for EP_CFG register
typedef union {
  struct {
    unsigned EGY_PWR_EN : 1;        // Bit 0 EGY_PWR_EN: Habilita el acumulador de energía y potencia, cuando también se establece el bit de ejecución.
    unsigned EGY_TMR_MODE : 1;      // Bit 1 EGY_TMR_MODE: Determina si la energía se acumula en función del número de muestras de 8 kSPS o eventos de cruce por cero configurados en el registro EGY_TIME.
    unsigned RESERVED_1 : 2;        // Bits [2:3] RESERVED: Reservado
    unsigned EGY_LD_ACCUM : 1;      // Bit 4 EGY_LD_ACCUM: Determina si el registro de energía interno sobrescribe el registro de energía accesible por el usuario cuando ocurre el evento EGYRDY.
    unsigned RD_RST_EN : 1;         // Bit 5 RD_RST_EN: Habilita la lectura del registro de energía con la función de reset. Si este bit está configurado, cuando se lee uno de los registros xWATTHR, xVAHR, xVARH, xFWATTHR, xFVAHR y xFVARHR, se reinicia y comienza a acumular energía desde cero.
    unsigned PWR_SIGN_SEL_0 : 1;    // Bit 6 PWR_SIGN_SEL[0]: Selecciona si el bit REVAPx sigue el signo de la potencia activa total o fundamental.
    unsigned PWR_SIGN_SEL_1 : 1;    // Bit 7 PWR_SIGN_SEL[1]: Selecciona si el bit REVRPx sigue el signo de la potencia reactiva total o fundamental.
    unsigned RESERVED_2 : 5;        // Bits [8:12] RESERVED: Reservado
    unsigned NOLOAD_TMR : 3;        // Bits [13:15] NOLOAD_TMR: Configura cuántas muestras de 8 kSPS evaluar sobre la condición de no carga.
  };
  uint16_t raw;
} ADE_EP_CFG_t;


// Struct for CRC_FORCE register
typedef union {
  struct {
    unsigned RESERVED : 15;           // Bits [1:15] RESERVED: Reserved
    unsigned FORCE_CRC_UPDATE : 1;   // Bit 0 FORCE_CRC_UPDATE: Write this bit to force the configuration register CRC calculation to start. When the calculation is complete, the CRC_DONE bit is set in the STATUS1 register.
  };
  uint16_t raw;
} ADE_CRC_FORCE_t;



// Struct for CRC_OPTEN register
typedef union {
  struct {
    unsigned CRC_MASK0_EN : 1;       // Bit 0 CRC_MASK0_EN: Set this bit to include the MASK0 register in the configuration register CRC calculation.
    unsigned CRC_MASK1_EN : 1;       // Bit 1 CRC_MASK1_EN: Set this bit to include the MASK1 register in the configuration register CRC calculation.
    unsigned CRC_EVENT_MASK_EN : 1;  // Bit 2 CRC_EVENT_MASK_EN: Set this bit to include the EVENT_MASK register in the configuration register CRC calculation.
    unsigned CRC_DIP_LVL_EN : 1;     // Bit 3 CRC_DIP_LVL_EN: Set this bit to include the DIP_LVL register in the configuration register CRC calculation.
    unsigned CRC_DIP_CYC_EN : 1;     // Bit 4 CRC_DIP_CYC_EN: Set this bit to include the DIP_CYC register in the configuration register CRC calculation.
    unsigned CRC_SWELL_LVL_EN : 1;   // Bit 5 CRC_SWELL_LVL_EN: Set this bit to include the SWELL_LVL register in the configuration register CRC calculation.
    unsigned CRC_SWELL_CYC_EN : 1;   // Bit 6 CRC_SWELL_CYC_EN: Set this bit to include the SWELL_CYC register in the configuration register CRC calculation.
    unsigned CRC_ACT_NL_LVL_EN : 1;  // Bit 7 CRC_ACT_NL_LVL_EN: Set this bit to include the ACT_NL_LVL register in the configuration register CRC calculation.
    unsigned CRC_REACT_NL_LVL_EN : 1; // Bit 8 CRC_REACT_NL_LVL_EN: Set this bit to include the REACT_NL_LVL register in the configuration register CRC calculation.
    unsigned CRC_APP_NL_LVL_EN : 1;   // Bit 9 CRC_APP_NL_LVL_EN: Set this bit to include the APP_NL_LVL register in the configuration register CRC calculation.
    unsigned CRC_ZXTOUT_EN : 1;       // Bit 10 CRC_ZXTOUT_EN: Set this bit to include the CRC_ZXTOUT_EN register in the configuration register CRC calculation.
    unsigned CRC_ZXLPSEL_EN : 1;      // Bit 11 CRC_ZXLPSEL_EN: Set this bit to include the ZX_LP_SEL register in the configuration register CRC calculation.
    unsigned CRC_SEQ_CYC_EN : 1;      // Bit 12 CRC_SEQ_CYC_EN: Set this bit to include the SEQ_CYC register in the configuration register CRC calculation.
    unsigned CRC_WFB_CFG_EN : 1;      // Bit 13 CRC_WFB_CFG_EN: Set this bit to include the WFB_CFG register in the configuration register CRC calculation.
    unsigned CRC_WFB_PG_IRQEN : 1;    // Bit 14 CRC_WFB_PG_IRQEN: Set this bit to include the WFB_PG_IRQEN register in the configuration register CRC calculation.
    unsigned CRC_WFB_TRG_CFG_EN : 1;  // Bit 15 CRC_WFB_TRG_CFG_EN: Set this bit to include the WFB_TRG_CFG register in the configuration register CRC calculation.
  };
  uint16_t raw;
} ADE_CRC_OPTEN_t;


// Struct for TEMP_CFG register
typedef union {
  struct {
    unsigned TEMP_TIME : 2;         // Bits [0:1] TEMP_TIME: Select the number of temperature readings to average.
    unsigned TEMP_EN : 1;           // Bit 2 TEMP_EN: Set this bit to enable the temperature sensor.
    unsigned TEMP_START : 1;        // Bit 3 TEMP_START: Set this bit to manually request a new temperature sensor reading.
    unsigned RESERVED : 11;         // Bits [4:14] RESERVED: Reserved
    unsigned RESERVED_2 : 1;        // Bit 15 RESERVED: Reserved
  };
  uint16_t raw;
} ADE_TEMP_CFG_t;


// Struct for TEMP_RSLT register
typedef union {
  struct {
    unsigned TEMP_RESULT : 12;      // Bits [0:11] TEMP_RESULT: 12-bit temperature sensor result.
    unsigned RESERVED : 4;          // Bits [12:15] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_TEMP_RSLT_t;


// Struct for PGA_GAIN register
typedef union {
  struct {
    unsigned IA_GAIN : 2;           // Bits [0:1] IA_GAIN: PGA gain for Current Channel A ADC.
    unsigned IB_GAIN : 2;           // Bits [2:3] IB_GAIN: PGA gain for Voltage Channel B ADC.
    unsigned IC_GAIN : 2;           // Bits [4:5] IC_GAIN: PGA gain for Current Channel C ADC.
    unsigned IN_GAIN : 2;           // Bits [6:7] IN_GAIN: PGA gain for neutral current channel ADC.
    unsigned VA_GAIN : 2;           // Bits [8:9] VA_GAIN: PGA gain for Voltage Channel A ADC.
    unsigned VB_GAIN : 2;           // Bits [10:11] VB_GAIN: PGA gain for Voltage Channel B ADC.
    unsigned VC_GAIN : 2;           // Bits [12:13] VC_GAIN: PGA gain for voltage Channel C ADC.
    unsigned RESERVED : 2;          // Bits [14:15] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_PGA_GAIN_t;

// Struct for CHNL_DIS register
typedef union {
  struct {
    unsigned IA_DISADC : 1;         // Bit 0 IA_DISADC: Set this bit to one to disable the ADC for Current Channel A.
    unsigned IB_DISADC : 1;         // Bit 1 IB_DISADC: Set this bit to one to disable the ADC for Voltage Channel B.
    unsigned IC_DISADC : 1;         // Bit 2 IC_DISADC: Set this bit to one to disable the ADC for Current Channel C.
    unsigned IN_DISADC : 1;         // Bit 3 IN_DISADC: Set this bit to one to disable the ADC for neutral current channel.
    unsigned VA_DISADC : 1;         // Bit 4 VA_DISADC: Set this bit to one to disable the ADC for Voltage Channel A.
    unsigned VB_DISADC : 1;         // Bit 5 VB_DISADC: Set this bit to one to disable the ADC for Voltage Channel B.
    unsigned VC_DISADC : 1;         // Bit 6 VC_DISADC: Set this bit to one to disable the ADC for Voltage Channel C.
    unsigned RESERVED : 9;          // Bits [7:15] RESERVED: Reserved
  };
  uint16_t raw;
} ADE_CHNL_DIS_t;

// Struct for VAR_DIS register
typedef union {
  struct {
    unsigned VARDIS : 1;  // Bit 0: Set this bit to disable the total VAR calculation
    unsigned RESERVED : 15; // Bits [15:1]: Reserved
  };
  uint16_t raw;
} ADE_VAR_DIS_t;




#endif