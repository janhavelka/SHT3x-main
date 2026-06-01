# SHT3x Alert Mode Application Note

- Source PDF: `docs/SHT3x_HT_AN_AlertMode.pdf`
- Extraction date: 2026-05-09
- Page count: 5
- SHA256: `ed55b9691bcb8598391f2d740a54520a24662a91090a77cd20d1529b0de6497d`
- Extraction tool: pypdf

## Page 1

```text
www.sensirion.com Version 3.1 - November 2023 1 /5
Alert Mode of SHT3x- and STS3x-DIS
Humidity and temperature watchdog functionality
SHT3x-DIS and STS3x -DIS feature an Alert Mode , when
operated in the periodic data acquisition mode1. The Alert
Mode allows to monitor the environmental condition
(humidity and temperature) relative to programmable limits.
When limits are reached the value of a dedicated ALERT pin
will change. Additionally, a status register bit indicates the
cause of the alert . Using the ALERT pin allows to create a
switch with a minimal bill of materials.
For example, only a transistor is needed next to the sensor
to switch an LED on or start a climate control.
Alternatively, the ALERT pin may be connected to the
interrupt pin of a microcontroller. After an alert from
SHT3x/STS3x the microcontroller can wake -up from sleep
mode and then perform certain actions.
The ALERT pin is configured push-pull.

1 Activation and Deactivation of the Alert
Mode
Whenever the sensor operates in periodic data acquisiton
mode the alert mode is active. It is possible to deactivate the
limit for temperature and humidity individually, by setting the
Minimum set point to values higher than the Maximum set
point (LowSet>HighSet for deactivation of the alert mode).
2 Alert Mode Limits
The limits can be controlled by the user through the
corresponding commands (see Table 3). SHT3x-DIS allows
to set different limits for temperature and humidity as well as
for high and low limits. STS3x-DIS only offers setting of
temperature limits. Additionally, the activation and the
deactivation of the alert can be controlled separa tely from
another, by choosing the set and clear limit appropriately.
This allows to remove fast oscillations of the ALERT pin
close to set limit values. The different limits are shown in
Figure 1.

1 See Datasheet SHT3x-DIS and STS3x -DIS Section 4 for the
distinction between periodic and single shot mode.

2.1 Data Format for the Alert Limits
SHT3x-DIS
The sensor stores the limit information in a reduced data
format. The standard data format of SHT3x-DIS has a width
of 16 bits. For the limits only the most significant bits (MSB)
are used to determine whether an alert has been met (7 bits
for humidity and 9 bits for temperature), see Figure 2.

Figure 2 Relevant bits for the SHT3x limits
This allows to transfer temperature and humidity limits with
the same command and to process them internally more
efficient.
As a consequence the limits have a different resolution than
the measurement values. The resolution of the temperature
limits are T~=0.5 deg C, whereas the humidity limits can be set
with a resolution of RH~=1%. Please note that data is
always measured and stored in the full 16 bit format. The
reduced data format is only used to judge whether an alert
condition is met.
STS3x-DIS
In the case of the STS3x the limits are composed as shown
in Figure 3.

Figure 1 Different limits for the Alert Mode
Temperature
or Humidity
Alert Pin
Output high
low
LOW Set
LOW Clear
HIGH Set
HIGH Clear
High Alert ON
Low Alert ON
Alert OFF
```

## Page 2

```text
Alert Mode of SHT3x-DIS
www.sensirion.com Version 3.1 - November 2023 2 /5

Figure 3 Relevant bits for the STS3x limits. N.B.: The first
seven bits are always zero.
2.2 Standard values for the Alert Limits
During power-up or after resets pre-defined limits are loaded
into the register, see Table 1. The standard limits are
"guarding" the grey area depicted in Figure 4.
Alert Limit
Initial Value
Physical Value
(RH/T)
Hex Value
high alert
limit
set limit 80% / 60 deg C 0x CD 33
clear limit 79% / 58 deg C 0x C9 2D
Low alert
limit
Clear limit 22% / -9 deg C 0x 38 69
set limit 20% / -10 deg C 0x 34 66
Table 1 Initial values for the alert limits. For STS3x, only the
temperature limits are set. The limits can be changed with the
command shown in Table 2.

Figure 4 The alert is active in the areas outside the shaded area
for SHT3x. For STS3x, the alert is active from -10 deg C ... 60 deg C.
2.3 Setting different limits
All limits can be read-out and written through the commands
shown in Table 3. The detailed I2C communication for read-
out of the currently set limits is shown in Table 3. Setting of
limits is detailed in Table 4.
Command Hex Code
Command
MSB
Command
LSB
READ
High alert
limit
set
0xE1
1F
clear 14
Low alert
limit
clear 09
set 02
WRITE
High alert
limit
set
0x61
1D
clear 16
Low alert
limit
clear 0B
set 00
Table 2 Commands to read and write all eight alert limits.

Table 3 Alert limit commands for reading the alert limits. For
STS3x, the 7bit MSB are to be neglected.

Table 4 Alert limit commands for writing the alert limits. For
STS3x, the 16 bit limit value is composed as shown in Figure 3.
2.4 Typical procedure to calculate the limits
The reduced data format is shown in Figure 2.
1. Choose the limits for RH and T (e.g. MaxSet limit,
RHMaxSet=80% & TMaxSet=60 deg C)
2. Convert the RHMaxSet and the TMaxSet limits to their
respective 16 bit binary value
a. RHMaxSet= 1100'1100'1100'1101
b. TMaxSet= 1001'1001'1001'1010
3. Remove the 9 LSBs of the RHMaxSet limits
a. RHMaxSet=1100'1100'1100'1101
4. Remove the 7 LSBs of the TMaxSet limits
a. TMaxSet=1001'1001'1001'1010
5. Combine the reduced values (step 3 and 4 )
according to Figure 2
a. RH,TMaxSet=1100'1101'0011'0011=0x
E699
Relative humidity (%)
Temperature ( deg C)
0
20
40
60
80
100
Normal Range
Max.
Range
ACK
S
ACK
ACK
ACK
ACK
ACK
Data MSB Data LSB
16-bit command
I2C Address Command MSB Command LSB
P
checksum
CRC
I2C write header
I2C Address
I2C read header
R
ACK
ACK
S
ACK
ACK
ACK
ACK
ACK P
ACK
Data MSB Data LSB
checksum
16-bit commandI2C write header
I2C Address Command MSB Command LSB
CRC
```

## Page 3

```text
Alert Mode of SHT3x-DIS
www.sensirion.com Version 3.1 - November 2023 3 /5

6. Calculate the 8 bit CRC from the 16 bit limit value
An excel sheet is supplied as well that can be used to
calculate the Alert limits.
2.5 Typical procedure to change the alert
condition
1. Calculate the limits as explained in section 2.4 (the
predefined values are the normal range and shown
in Table 1)
2. Set the periodic frequency to the desired value
through issuing of the appropriate command
The alert mode is now active.
3 Further condition that can raise an alert
The ALERT pin will also become active (high) after power -
up and after resets, regardless whether the later was
triggered by a brown-out, by a user command (general call)
or via the nRESET pin.
Description of the Content of the status register
Bit Field description Default
value
15 Alert pending status
'0': no pending alerts
'1': at least one pending alert
'1'
14 Reserved '0'
13 Heater status
'0' : Heater OFF
'1' : Heater ON
'0'
12 Reserved '0'
11 RH tracking alert
'0' : no alert
'1' . alert
'0
10 T tracking alert
'0' : no alert
'1' . alert
'0'
9:5 Reserved '00000'
4 System reset detected
'0': no reset detected since last 'clear
status register' command

'1': reset detected (hard reset,general call
reset or supply fail)
'1'
3:2 Reserved '00'
1 Command status
'0': last command executed successfully
'1': last command not processed. It was
either invalid, failed the integrated
command checksum
'0'
0 Write data checksum status
'0': checksum of last write transfer was
correct
'1': checksum of last write transfer failed
'0'
Table 5 Status register of SHT3x

3.1 Readout the status register
Command Hex Code
Read Status register 0x F3 2D

Table 6 Read out status register. The content of the user register
is described below ( Clear blocks are controlled by the
microcontroller, grey blocks by the sensor.)
3.2 Clear Status register
All flags (Bit 15, 11, 10, 4) in the status register (Table 5) can
be cleared (set to zero) by sending the command shown in
Table 7.
Command Hex Code
Clear Status Register 0x 30 41

Table 7 Command to clear all status register flags (Clear
blocks are controlled by the microcontroller, grey blocks by
the sensor.)
3.3 Behaviour in the case of brown-out or power-
up
If a brown -out or power-up occurs, the sensor will restart
automatically. This sets all values to the default values
(Table 1). Therefore, all customer defined limits are lost. As
explained above an Alert is issued in this case.
```

## Page 4

```text
Alert Mode of SHT3x-DIS
www.sensirion.com Version 3.1 - November 2023 4 /5

Revision History
Date Version Page(s) Changes
May 2015 1 all Release of Version 1
September 2019 2 All Including STS3x
October 2021 3 Included push-pull configuration of Alert pin
November 2023 3.1 3 Corrected when a system reset is detected in Table 5.
```

## Page 5

```text
Alert Mode of SHT3x-DIS
www.sensirion.com Version 3.1 - November 2023 5 /5

Important Notices
Warning, Personal Injury
Do not use this product as safety or emergency stop
devices or in any other application where failure of the
product could result in personal injury. Do not use this
product for applications other than its intended and
authorized use. Before installing, handling, using or
servicing this product, please consult the data sheet and
application notes. Failure to comply with these instructions
could result in death or serious injury.

If the Buyer shall purchase or use SENSIRION products for any
unintended or unauthorized application, Buyer shall defend,
indemnify and hold harmless SENSIRION and its officers,
employees, subsidiaries, affiliates and distributors against all
claims, costs, damages and expenses, and reasonable attorney
fees arising out of, directly or indirectly, any claim of personal
injury or death associated with such unintended or unauthorized
use, even if SENSIRION shall be allegedly negligent with
respect to the design or the manufacture of the product.
ESD Precautions
The inherent design of this component causes it to be sensitive
to electrostatic discharge (ESD). To prevent ESD-induced
damage and/or degradation, take customary and statutory ESD
precautions when handling this product.
See application note "ESD, Latchup and EMC" for more
information.
Warranty
SENSIRION warrants solely to the original purchaser of this
product for a period of 12 months (one year) from the date of
delivery that this product shall be of the quality, material and
workmanship defined in SENSIRION's published specifications
of the product. Within such period, if proven to be defective,
SENSIRION shall repair and/or replace this product, in
SENSIRION's discretion, free of charge to the Buyer, provided
that:
- notice in writing describing the defects shall be given to
SENSIRION within fourteen (14) days after their
appearance;
- such defects shall be found, to SENSIRION's reasonable
satisfaction, to have arisen from SENSIRION's faulty
design, material, or workmanship;
- the defective product shall be returned to SENSIRION's
factory at the Buyer's expense; and
- the warranty period for any repaired or replaced product
shall be limited to the unexpired portion of the original
period.
This warranty does not apply to any equipment which has not
been installed and used within the specifications recommended
by SENSIRION for the intended and proper use of the
equipment. EXCEPT FOR THE WARRANTIES EXPRESSLY
SET FORTH HEREIN, SENSIRION MAKES NO
WARRANTIES, EITHER EXPRESS OR IMPLIED, WITH
RESPECT TO THE PRODUCT. ANY AND ALL WARRANTIES,
INCLUDING WITHOUT LIMITATION, WARRANTIES OF
MERCHANTABILITY OR FITNESS FOR A PARTICULAR
PURPOSE, ARE EXPRESSLY EXCLUDED AND DECLINED.
SENSIRION is only liable for defects of this product arising
under the conditions of operation provided for in the data sheet
and proper use of the goods. SENSIRION explicitly disclaims all
warranties, express or implied, for any period during which the
goods are operated or stored not in accordance with the
technical specifications.
SENSIRION does not assume any liability arising out of any
application or use of any product or circuit and specifically
disclaims any and all liability, including without limitation
consequential or incidental damages. All operating parameters,
including without limitation recommended parameters, must be
validated for each customer's applications by customer's
technical experts. Recommended parameters can and do vary
in different applications.
SENSIRION reserves the right, without further notice, (i) to
change the product specifications and/or the information in this
document and (ii) to improve reliability, functions and design of
this product.

Copyright (c) 2019, by SENSIRION.
CMOSens(R) is a trademark of Sensirion
All rights reserved
Headquarters and Subsidiaries
SENSIRION AG
Laubisruetistr. 50
CH-8712 Staefa ZH
Switzerland

phone: +41 44 306 40 00
fax: +41 44 306 40 30
info@sensirion.com
www.sensirion.com
Sensirion Inc. USA
phone: +1 312 690 5858
info-us@sensirion.com
www.sensirion.com

Sensirion Japan Co. Ltd.
phone: +81 3 3444 4940
info-jp@sensirion.com
www.sensirion.co.jp
Sensirion Korea Co. Ltd.
phone: +82 31 337 7700~3
info-kr@sensirion.com
www.sensirion.co.kr

Sensirion China Co. Ltd.
phone: +86 755 8252 1501
info-cn@sensirion.com
www.sensirion.com.cn/
Sensirion Taiwan Co. Ltd.
phone: +41 44 306 40 00
info@sensirion.com
To find your local representative, please visit www.sensirion.com/contact
```
