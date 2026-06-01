# Sensirion Humidity Sensors at a Glance

- Source PDF: `docs/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf`
- Extraction date: 2026-05-09
- Page count: 3
- SHA256: `29edba4ba5320e04aa3973ea0ab985b54a9acb15a537ff74cc1a896c8d588036`
- Extraction tool: pypdf

## Page 1

```text
(c) Copyright Sensirion AG, Switzerland 1/3
Humidity at a glance

Abstract
This summary provides an overview on the most used humidity related formulas. The sample code is optimized
for microprocessors (e.g. the common logarithm "log10" is used rather than the natural logarithm "ln"). For an in-
depth explanation of the equations please refer to our complementary information note "Introduction to Humidity".

1 Relative humidity
The following formula relates two states (temperature and relative humidity) of a system. It is only valid at constant
absolute humidity (e.g. closed system).

RH2 = RH1 exp (m Tn T1 - T2
(Tn + T1)(Tn + T2))
RH1
RH2
T1
T2
m
Tn

Relative humidity at state 1
Relative humidity at state 2
Temperature [ deg C] at state 1
Temperature [ deg C] at state 2
17.62
243.21 deg C

Sample code:
RH2 = RH1*exp(4283.78*(T1-T2)/(243.12+T1)/(243.12+T2));

2 Dew point
The dew point is the temperature at which air must be cooled (at constant barometric pressure) so that water vapor
starts condensing into liquid water.

Td(T, RH)= Tn
ln (RH
100)+ m T
Tn + T
m - (ln (RH
100)+ m T
Tn + T)

Td
T
RH
m
Tn

Dew point temperature [ deg C]
Actual temperature [ deg C]
Actual relative humidity [%]
17.62
243.21 deg C

Sample code:
RH = max(0, min(RH,100));
h = (log10(RH)-2.0)/0.4343+(17.62*T)/(243.12+T);
Td = 243.12*h/(17.62-h);

3 Absolute humidity
The absolute humidity is the mass of water vapor in a particular volume of dry air. The unit is [g/m3].

V(T, RH)= 216.7 (
RH
100 A exp ( m T
Tn + T)
273.15 + T )
V
T
RH
m
Tn
A
Absolute humidity [g/m3]
Actual temperature [ deg C]
Actual relative humidity [%]
17.62
243.21 deg C
6.112 hPa
```

## Page 2

```text
(c) Copyright Sensirion AG, Switzerland 2/3

Sample code:
RH = max(0, min(RH,100));
rho_v = 216.7*(RH/100.0*6.112*exp(17.62*T/(243.12+T))/(273.15+T));

4 Mixing ratio
The mixing ratio is the mass of water vapor in a particular mass of dry air. The unit is [g/kg].

r (T, RH)=
622 RH
100 A exp ( m T
Tn + T)
p - RH
100 A exp ( m T
Tn + T)

r
T
RH
p
m
Tn
A
Absolute humidity [g/m3]
Actual temperature [ deg C]
Actual relative humidity [%]
Barometric air pressure [hPa]
17.62
243.21 deg C
6.112 hPa

Sample code:
RH = max(0, min(RH,100));
e = RH/100.0*6.112*exp(17.62*T/(243.12+T));
r = 622.0*e/(p-e);

5 Heat index
The heat index expresses the "felt" air temperature based on actual air temperature and relative humidity. It is
determined according to the National Weather Service and Weather Forecast Office of the National Oceanic and
Atmospheric Administration (NOAA).

HI = c1 + c2 T + c3 RH + c4 T RH + c5 T2 + c6 R2
+ c7 T2RH + c8 T RH2 + c9 T2 RH2

adj1 = 5
9 (RH - 13
4
sqrt17 - |1.8 T - 63|
17 - 32)

adj2 = 5
9 (
(RH - 85)(55 - 1.8 T)
50 - 32)

HI
T
RH
c1
c2
c3
c4
c5
c6
c7
c8
c9
Heat index [ deg C]
Actual temperature [ deg C]
Actual relative humidity [%]
- 8.78469475556
1.61139411
2.33854883889
- 0.14611605
- 0.012308094
- 0.0164248277778
0.002211732
0.00072546
- 0.000003582

If RH < 13% and 26.7 deg C < T < 44.4 deg C, then one should correct HI with the first adjustment term:
HI <- HI + adj1

If RH > 85% and 26.7 deg C < T < 30.6 deg C, then one should correct HI with the second adjustment term:
HI <- HI + adj2

In practice, one should first compute the simple formula for HI given below and average its result with the actual
temperature. If the averaged value is 26.7 deg C or higher, the full regression equation with possible adjustments as
described above is applied - else the result of the simple formula can be kept.

HI = 5
9 ( 1.98 T - 7.1 + 0.047 RH )
HI
T
RH
Heat index [ deg C]
Actual temperature [ deg C]
Actual relative humidity [%]
```

## Page 3

```text
(c) Copyright Sensirion AG, Switzerland 3/3
Sample code:
RH = max(0, min(RH,100));
HI = 1.1*T + 5*(0.047*RH - 7.1)/9;
If (HI+T)/2 >= 26.7 Then {
HI = - 8.78469475556
+ 1.61139411*T
+ 2.33854883889*RH
- 0.14611605*T*RH
- 0.012308094*T*T
- 0.0164248277778*RH*RH
+ 0.002211732*T*T*RH
+ 0.00072546*T*RH*RH
- 0.000003582*T*T*RH*RH;
If RH<13 && T>26.7 && T<44.4 Then
HI = HI + (5/36)*(RH-13)*sqrt((17-abs(1.8*T-63))/17)
- 160/9;
If RH>85 && T>26.7 && T<30.6 Then
HI = HI + 5*(RH-85)*(55-1.8*T)/450 - 160/9;
}

Version history

Date Revision Changes
Aug. 2008 1.0 Initial version
Jun. 2021 2.0 Corrected heat index definition, updated design
May 2025 2.1 Modified sample code

Headquarter and subsidiaries

Sensirion AG
Laubisrtistr. 50
8712 Stfa
Switzerland

phone: +41 44 306 40 00
fax: +41 44 306 40 30
info@sensirion.com
www.sensirion.com
Sensirion Inc., USA
phone: +1 312 690 5858
info-us@sensirion.com
www.sensirion.com
Sensirion Korea Co. Ltd.
phone: +82 31 337 7700~3
info-kr@sensirion.com
www.sensirion.com/kr
Sensirion Japan Co. Ltd.
phone: +81 3 3444 4940
info-jp@sensirion.com
www.sensirion.com/jp
Sensirion China Co. Ltd.
phone: +86 755 8252 1501
info-cn@sensirion.com
www.sensirion.com/cn
Sensirion Taiwan Co. Ltd
phone: +886 3 5506701
info@sensirion.com
www.sensirion.com To find your local representative, please visit www.sensirion.com/distributors
```
