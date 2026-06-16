# Sensirion Electronic Identification Code for SHT3x

- Source PDF: `docs/Sensirion_electronic_identification_code_SHT3x.pdf`
- Extraction date: 2026-05-09
- Page count: 3
- SHA256: `ba871d1166047bfc4a35adeb45b4e31b344595899b5ef9c5e1c913c582fe1071`
- Extraction tool: pypdf

## Page 1

```text
www.sensirion.com D1 Version 3 - January 2023 1/3
SHT3x Electronic Identification Code
How to read-out the serial number of SHT3x

The SHT3x provides a device specific serial number which can be read-out via the serial interface (I2C). The
Serial number allows an unambiguous identification of each individual device. This application note
describes the procedure to read-out the serial number.

Communication Sequence
The communication sequence for reading out the serial number complies with the general I2C serial
interface protocol. For detailed information on I2C, please refer to the SHT3x Datasheet and the I 2C bus
specification by NXP Semiconductors [1].

Command Clock streching Hex Code
Get Serial Number enabled 0x 37 80
disabled 0x 36 82

Table 1. Command to read out the Serial Number (Clear blocks are
controlled by the microcontroller, grey blocks by the sensor.)

After issuing the measurement command and sending the ACK Bit the sensor needs the time t IDLE = 1ms
to respond to the I2C read header with an ACK Bit. Hence it is recommended to wait t IDLE = 1 ms before
issuing the read header. The Get Serial Number command returns 2 words, every word is followed by a
CRC Checksum. Together the 2 words (SNB_3 to SNB_0 in Table 1, SNB_0 is the LSB, whereas SNB_3 is the
MSB) constitute a unique serial number with a length of 32 bit. This serial number can be used to identify
each sensor individually.

ACK
ACK
ACKACKtIDLE
9 36
SNB_3 SNB_2 CRC
ACK
ACK
ACK
Serial Number Word 1 Checksum
37 63
SNB_1 SNB_0 CRC
ACK
ACK
ACK
Serial Number Word 2 Checksum
P
SNB_3
I2C Address Command MSB Command LSB
16-bit command16-bit commandI2C write header
I2C read header
I2C Address
S W
S R
1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27
1 2 3 4 5 6 7 8
ACKACK
9
```

## Page 2

```text
www.sensirion.com D1 Version 3 - January 2023 2/3
Bibliography

[1] NXP Semiconductors, "User manual UM10204," Bd. Rev. 6, 2014.

Revision History
Date Version Page(s) Changes
November 2014 0.9 Release of D2 Version
May 2015 1 Release of Version 1
February 2018 2 1 Change of tIDLE to 1ms
January 2023 3 All
All
1
2
Change of confidentiality to D1
Reformatting
Changed Reference to NXP user manual (nonfunctioning link)
Addition of Bibliography
```

## Page 3

```text
www.sensirion.com D1 Version 3 - January 2023 3/3
Important Notices
Warning, Personal Injury
Do not use this product as safety or emergency stop devices or in any other application where failure of the product could
result in personal injury. Do not use this product for applications other than its intended and authorized use. Before installing,
handling, using or servicing this product, please consult the data sheet and application notes. Failure to comply with these
instructions could result in death or serious injury.
If the Buyer shall purchase or use SENSIRION products for any unintended or unauthorized application, Buyer shall defend,
indemnify and hold harmless SENSIRION and its officers, employees, subsidiaries, affiliates and distributors against all claims, costs,
damages and expenses, and reasonable attorney fees arising out of, directly or indirectly, any claim of personal injury or de ath
associated with such unintended or unauthorized use, even if SENSIRION shall be allegedly negligent with respect to the design
or the manufacture of the product.
ESD Precautions
The inherent design of this component causes it to be sensitive to electrostatic discharge (ESD). To prevent ESD-induced damage
and/or degradation, take customary and statutory ESD precautions when handling this product. See application note "ESD, Latchup
and EMC" for more information.
Warranty
SENSIRION warrants solely to the original purchaser of this product for a period of 12 months (one year) from the date of delivery
that this product shall be of the quality, material and workmanship defined in SENSIRION's published specifications of the product.
Within such period, if proven to be defective, SENSIRION shall repair and/or replace this product, in SENSIRION's discretion, free
of charge to the Buyer, provided that:
- notice in writing describing the defects shall be given to SENSIRION within fourteen (14) days after their appearance;
- such defects shall be found, to SENSIRION's reasonable satisfaction, to have arisen from SENSIRION's faulty design, material,
or workmanship;
- the defective product shall be returned to SENSIRION's factory at the Buyer's expense; and
- the warranty period for any repaired or replaced product shall be limited to the unexpired portion of the original period.
This warranty does not apply to any equipment which has not been installed and used within the specifications recommended by
SENSIRION for the intended and proper use of the equipment. EXCEPT FOR THE WARRANTIES EXPRESSLY SET FORTH HEREIN,
SENSIRION MAKES N O WARRANTIES, EITHER EXPRESS OR IMPLIED, WITH RESPECT TO THE PRODUCT. ANY AND ALL
WARRANTIES, INCLUDING WITHOUT LIMITATION, WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR
PURPOSE, ARE EXPRESSLY EXCLUDED AND DECLINED.
SENSIRION is only liable for defects of this product arising under the conditions of operation provided for in the data sheet and
proper use of the goods. SENSIRION explicitly disclaims all warranties, express or implied, for any period during which the goods
are operated or stored not in accordance with the technical specifications.
SENSIRION does not assume any liability arising out of any application or use of any product or circuit and specifically disc laims
any and all liability, including without limitation consequential or incidental damages. All operating parameters, including without
limitation recommended parameters, must be validated for each customer's applications by customer's technical experts.
Recommended parameters can and do vary in different applications.
SENSIRION r eserves the right, without further notice, (i) to change the product specifications and/or the information in this
document and (ii) to improve reliability, functions and design of this product.
Headquarters and Subsidiaries
Sensirion AG
Laubisruetistr. 50
CH-8712 Staefa ZH
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
phone: +81 45 270 4506
info-jp@sensirion.com
www.sensirion.com/jp
Sensirion China Co. Ltd.
phone: +86 755 8252 1501
info-cn@sensirion.com
www.sensirion.com/cn
Sensirion Taiwan Co. Ltd
phone: +886 2 2218-6779
info@sensirion.com
www.sensirion.com
To find your local representative, please visit
www.sensirion.com/distributors
Copyright (c) 2022, by SENSIRION. CMOSens(R) is a trademark of Sensirion. All rights reserved
```
