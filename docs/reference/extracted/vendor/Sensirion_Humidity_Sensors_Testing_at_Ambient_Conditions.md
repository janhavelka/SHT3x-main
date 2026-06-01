# Sensirion Humidity Sensors Testing at Ambient Conditions

- Source PDF: `docs/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf`
- Extraction date: 2026-05-09
- Page count: 10
- SHA256: `45f19fbbd44c0fe3aad13e79a0fd8c74114e1bf86bdc4d68de978464861e4765`
- Extraction tool: pypdf

## Page 1

```text
www.sensirion.com / D1 Version 3 - October 2022 1/10

Testing at Ambient Conditions

Preface

Sensirion Humidity & Temperature sensors are
individually tested and fully factory-calibrated by
Sensirion and it should not be necessary to test the
sensor function after assembly. However, it may be
desired to test the sensor during in-line or final test as a
way to verify that the sensor had been correctly stored,
handled and assembled.

While equipment and techniques exist, to measure
temperature and relative humi dity accurately, those
may not be feasible or cost efficient for mass
production. Testing at ambient conditions instead may
provide an appropriate compromise. To prevent
instable ambient conditions that lead to large spread in
the measurements, several gui delines should be
considered relating to the design of the test jig, the test
procedure and the test environment.

Applicability
This document is applicable to products that contain
Sensirion SHTxx r elative humidity & temperature
sensors and will undergo relative humidity (RH) and
temperature (T) test at ambient conditions . This may
apply to test of sensors on PCB or inside final products.
This application note provides step-by-step guidelines
for realizing a capable test system:

1. selection of an appropriate reference sensor
2. hardware design of the test jig
3. software design of the test jig
4. setting up the test environment
5. defining the operation procedure
6. considering temporary humidity offset caused
by high temperature assembly processes
7. analyzing the capability of the measurement
system
8. defining appropriate test limits
Overview
Sensirion sensors are individually tested and fully
factory calibrated by Sensirion . Verification of the
calibration by the customer is not required. However, it
may be appropriate for the customer to test the sensor
after assembly, to verify that it had been correctly
stored, handled and assembled.

Testing humidity and temperature sensors accurately
requires the use of a climate chamber combined with
accurate measurement equipment for reference. Due to
the cost and limited throughput, such setups are rarely
appropriate for mass production.

For verifying a pre -calibrated sensor after a ssembly it
may be sufficient and most appropriate to test at
ambient conditions. Sensirion SHTxx h umidity &
temperature Sensors are very sensitive environment al
sensors. Hence, they are as well responding very
sensitively to the test environment. For a capable test
setup, the following main points need to be considered:

- select an accurate reference sensor
- optimize temperature coupling between the
reference sensor(s) and the device under test
- optimize humidity coupling between the
reference sensor(s) and the device under test
- minimize temperature and humidity gradients
and turbulences in the test environment

It is important that the device under test (DUT) and the
reference sensor (s) are at the same absolute
temperature. This may be obvious for temperature
measurement, but it is equally important for accurate
measurement of relative humidity. As relative humidity
is strongly temperature dependent, any temperature
difference between the DUT and the reference will lead
to a deviation in the relative humidity measurement.
Reference Sensor
The humidity and temperature values measured by the
Device Under Test (DUT) need to be compared to the
values of a reference sensor. The accuracy of the
reference sensor directly affects the over-all
measurement accuracy/capability of the setup .
Therefore, the reference sensor needs to be sufficiently
accurate in humidity and temperature.
Sensirion's pin-type SHT85 humidity & temperature
sensor is recommended as a reference sensor.

Figure 1 Sensirion SHT85 humidity and temperature sensor
backside gold plating
for optimal
thermal coupling
```

## Page 2

```text
www.sensirion.com / D1 Version 3 - October 2022 2/10

SHT85 offers high accuracy of +/-1. 5 %RH and +/-0.1 deg C,
a digital interface for easy integration and allows for a
flexible jig design . The Sensor is gold plated on the
backside, allowing for an excellent temperature
coupling to a surface or the environment. For more
information on the SHT 85 please refer to
https://sensirion.com/products/catalog/SHT85/

To reduce var iations and fluctuations of the measure d
reference value, two reference sensors may be
implemented. The average of both temperature values
and the average of both humidity values may be used
as the reference values.
Test Jig Design for Sensors on PCB
The following design recommendations apply to test
jigs for rigid or flexible PCBs.
Optimized Thermal Coupling
The best strategy is to couple the sensors not only via
the air but to thermally couple them through the test jig.
This should ensure more accurate m easurements of
temperature and humidity as well as shorter settling
times.

Figure 2 Exemplary data showing several sensors
approaching reference value. Different materials have been
used to couple the sensors to the reference sensor. Metal
offers the best thermal coupling, resulting in the fastest
settling times. Coupling sensors through e.g. plastic or foam
requires sign ificantly longer settling times and may lead to
less accurate measurements.
To ensure an optimal thermal coupling between the
reference sensor(s) and the sensor under test:
- Use a metal insert from e.g. aluminum or
copper. Ensure the metal insert is not creating
short circuits on the DUT.
- In case there are components on the back -
side of the DUT: add cavities to the insert of
the jig.

- Place the reference sensor as close as
mechanically possible to the sensor under
test (i.e. within a couple of millimeters only, if
mechanically possible ).

Figure 3 Good example of PCB test jig with a metal
insert, ensuring good thermal coupling between the
reference sensor and the sensor under test as well
as a minimum distance between them.
- Ensure the reference sensor is in good
contact with the metal insert of the jig. If
necessary, use a clip to hold down the
reference sensor but ensure the sensor cavity
is not obstructed.
- Ensure the DUT is in good contact with the
metal insert of the jig. If necessary, add a
spring that presses the DUT onto the metal
insert of the jig. It is recommended to press
down close to the sensor but not onto the
sensor directly as this could lead to particles in
the sensor cavity and obstruction of the sensor
cavity.
- Ideally, i nclude t wo reference sensors . This
will allow automatic detection of any problems
with the reference sensors themselves.

Figure 4 Good example of PCB test jig with clip holding down
two reference sensors to ensure good thermal coupling to the
metal insert. The reference sensors and the sensor under
test are placed at minimum distance to each other.
- De-couple the metal insert from an y
component on the DUT that may heat up
during test.
(side view)
(top view)
```

## Page 3

```text
www.sensirion.com / D1 Version 3 - October 2022 3/10

- De-couple (insulate) the metal insert from
any heat source in the environment (e.g.
isolate from any hot electronic circuits inside
the test jig).
- Preferably no electronic s are located inside
the mechanical test jig. If electronic s are
added inside the test jig, it is recommended to
add ventilation inside of the jig to prevent self-
heating of the jig.

Figure 5 Good example of PCB test jig with the metal insert
being decoupled from components on the DUT that heat up
during test. Metal insert is insulated from the self -heating of
the electronics inside the jig. Additionally the electronics of
the jig are ventilated to prevent too much self-heating.

Figure 6 Bad example of PCB test jig. Sensor under test and
reference sensor are not sufficiently close together and have
to rely on the thermal coupling through air alone. Additionally,
there are heat sources below the sensors creating a thermal
imbalance.
- Design the volume of the metal insert rather
large. The larger its thermal mass, the more it
will help stabilize the temperature of the
reference sensor and DUT.

- Extend the metal insert beyond the area of test
and include space to pre-stage units before
test. During pre-staging the unit's temperature
will have time to adjust to the temperature at
which it will be tested. This will help reduce
settling times.
- Prevent the operator from touching the metal
insert as the operator's body temperature may
cause a temperature variation of the metal
insert.

Figure 7 Good example of test jig top plate from thermally
insulating material (e.g. plastic), preventing the operator to
easily touch the metal insert of the test jig. The metal insert is
large enough to provide space for pre -staging several units
before test, al lowing the units to adjust to the temperature of
the jig.

Optimized Coupling of Humidity
To ensure an optimal humidity coupling, the reference
sensor(s) and the sensor under test need to be
exposed to the same, undisturbed atmosphere during
test:
- Enclose the reference sensor(s) and the
sensor under test within a cap of minimal
volume, insulated from the external
environment. (Make the volume under the cap
as small as mechanically possible, e.g. only a
very few cm3. Ensure the cap is not
obstructing the cav ities of the sensors .) The
smaller the volume, the shorter the settling
times required for a stable measurement. The
cap as well shields the sensors from
turbulences, thereby reducing measurement
variation. At the same time, the cap can press
down the PCB onto the metal insert of the jig
as suggested in Figure 4.
(side view)
(side view)
(top view)
```

## Page 4

```text
www.sensirion.com / D1 Version 3 - October 2022 4/10

Figure 8 Good Example of small cap enclosing just the
reference sensors and the sensor under test, minimizing the
volume under the cap as much as possible.
Test Jig Design for Sensors Inside a
Housing
In case of final products, the humidity and temperature
sensor is typically mounted inside a plastic housing. A
housing restricts the airflow and therefore a sensor in a
housing will always react slower than a sensor without
housing. This implies that longer settling times are
required during test for sensors in a housing.

The reference sensor(s) must be coupled to the sensor
under test through the surrounding air/atmosp here.
Following recommendations help optimize the thermal
and humidity coupling:
- Place the reference sensor as close as
possible to the sensor under test (i.e. within a
couple of millimeters only, if mechanically
possible). Only increase the distance if the
reference sensor might otherwise be warmed
up by dissipating heat from the unit under test
itself.

Figure 9 Good exa mple of test jig for sensor in a housing,
with sensor under test aligned just next to the reference
sensor, creating a small cavity with minimal volume,
protected from any turbulence in the environment.
- Enclose the reference sensor(s) and the
sensor under test within a cap of minimal
volume, insulated from the external
environment. (Make the volume under the cap
as small as mechanically possible, e.g. only a
very few cm3. Ensure the cap is not
obstructing the cavities of the sensors. ) The
smaller the volume, the shorter the settling
times required for a stable measurement. The
cap as well shields the sensors from
turbulences, thereby reducing measurement
variation.

Figure 10 Good example of reference sensor mounted inside
a cap, covering the sensor opening of the product housing.
The volume inside the cap is minimized, at the same time
protecting the sensors from turbulences in the environment.
- If enclosing the senso rs with a cap is not
possible, shield them from turbulences as
much as possible.

Figure 11 Bad example with reference sensor located far
from the sensor under test. Reference sensor is exposed to
hot air, while the sensor under test is cooled by a ven tilator
and/or air conditioning, creating a thermal imbalance. Neither
sensor is shielded from turbulences.
- If the unit is warm or warming up during test it
should be decoupled from the surface of the
test jig (this is to prevent the test jig warming
up over time, leading to a temperature drift
during a production run)

(side view)
(top view)
(side view)
(side view)
(side view)
```

## Page 5

```text
www.sensirion.com / D1 Version 3 - October 2022 5/10

Figure 12 Good example of self -heating unit, placed on
thermally insulating stilts to decouple it from the test jig
surface, preventing heating up of the jig. Note that the cap
around the reference sensor is as well detached from t he
device under test, to prevent it from heating up (and no
longer represent the environment correctly).
Test Software Design
When designing the software for the test jig, please
consider the following recommendations:
- As much as possible, prevent any component
on the DUT to heat up during test ( or the
sensor under test may heat up through
conductance of heat).
- Prevent self -heating of the sensors . (Sensors
should only be active/measuring during
maximum 10% of the time or sensor will start
to self-heat sli ghtly, which may influence the
temperature measurement.)
- If more than one reference sensor is used, test
their humidity and temperature values against
each other , to detect any problem related to
the reference sensors.
- Read out all sensors at the end of the test
sequence. Ensure the sensor signals had
sufficient time to settle to a stable value
before performing the measurements (see
below).
- Test the humidity and temperature values of
the sensor under test relative to t he reference
sensor(s). If more than one reference sensor
is used, test relative to the average values of
the reference sensors.
Tdelta = Tsensor_under_test - Treference_average
RHdelta = RHsensor_under_test - RHreference_average
- Datalog all measur ed values and a lways
maintain the signs of all values. (This will
allow to properly analyze the capability of the
measurement setup and performance of the
production.)
- Use appropriate test limits (see below).
- In case the test fails , test again after some
additional waiting time (immediate retest ). If
the second measurement is pass, the sensor
under test can be considered pass. (This will
help reduce problems due to variations in the
test environment and it is more efficient than
the operator having to start the test again.)

As mentioned in above list, there needs to be sufficient
time for the sensors to reach stable values.
To be able to evaluate appropriate setting/waiting times
the test setup should be able to measure and log a
device repeatedly in 1 second fixed intervals . The
values measured for several units can then be plotted
to derive an appropriate waiting time.

Figure 13 Graph showing RH delta measurements of several
units over time . The longer the wai ting time the more
accurate is the measurement (the closer it is to the final
value).
Settling time depends on many factors. Following the
recommendations outlined in this document will allow
shorter settling times. Shorter settling times imply that
the test system can reach higher throughput.
As can be seen from Figure 13 the longer the waiting
time is the smaller are as well the offset and variation in
the measurement. Reducing the waiting time increases
the variation and offset error in the measurement.
Hence there is a compromise be tween tight test limits
and short waiting times.
Test Environment
The test envir onment can have a significant impact on
the capability of the measurement setup.

The local humidity and temperature on the test jig may
significantly differ from other areas in the production
room. This implies that devices for test may be at a
different temperature and humidity level when arriving
at the test station and require longer settling times.

- Pre-stage units before test on top of (or just
beside) the test jig, to allow units to adjust to
local temperature and humidity (compare
Figure 7).
(side view)
```

## Page 6

```text
www.sensirion.com / D1 Version 3 - October 2022 6/10

Nearby heat sources (e.g. hot equipment), ventilation,
air conditioning as well as the movement of operators
and other production personnel can add significant local
variations and turbulence to the air around the test jig.
These turbulences will add variation to the
measurement.

- Reduce heat sources (e.g. heating or hot
equipment) near the test jig
- Prevent direct sunlight or areas that do heat
up during the day
- Avoid hot lighting onto the area. Use
fluorescent lights instead.

Figure 14 Bad example of a test jig that is exposed to
different heat sources - direct sunlight, a strong spotlight and
a heating system.
- Do not place the test jig directly exposed to a
strong draught from air-conditioning.
- Do not place the test jig next do doors or
windows, as this could lead to significant
draught of air with different temperature and
humidity.
- Limit movement of equipment and production
personnel in the area of the test jig in order to
reduce turbulences.
- Avoid fast moving air over the sensor . E.g.
do not use fans to blow directly at the sensors
as it may lead to discrepancies between the
sensors (Note: same as a human, a sensor
that is exposed to a wind will feel a 'chill factor'
and not correctly measure the real air
temperature but a lower than actual
temperature.).

To limit the effect of draughts and turbulences in the air,
it is recommended to shi eld the jig and create a gentle
flow of air over the test jig may help reduce turbulences
and operator influence.
- Make the test jig accessible for operation on
the front side only. Shield the test jig from
turbulence on all other sides.
- Add a computer fan into the top cover of the
shielding to create an even and gentle air
flow over the test jig and ensure that the air
sucked by the fan is at relatively stable
temperature and humidity. It shall be
assessed, if an ionizer fan is required (instead
of an ordinary computer fan).

Figure 15 Shielding the test jig to the left/right/back/top
reduce turbulences at the test jig. Access for operation is
from the front side only. A small fan in the top cover ensures
a gentle air flow towards the operator , further reducing the
effect of turbulence near the jig.
Operation Procedure
The handling instructions for handling of electronic
components (ESD protection) apply. As well, the
general handling instructions for Sensirion Humidity &
Temperature Sensors apply.
Additionally adhering to the following operation
guidelines may help further reduce measurement
variance:
- Handle the units as far as possible from the
sensor. This will minimize the effect of the
operator's body heat on the measurement.

Figure 16 Handle the u nits before test as far away from the
sensor as possible to prevent the operator's body heat to
warm up the DUT.
- To reduce the impact of the operator's breath
(exhaled air is rather warm and humid), the
operator may be wearing a face mask.
```

## Page 7

```text
www.sensirion.com / D1 Version 3 - October 2022 7/10

The local humidity and temperature on the test jig may
significantly differ from other areas in the production
room. This implies that devices for test may be at a
different temperature and h umidity level when arriving
at the test station and require longer settling times.
- Pre-stage units before test on top of (or just
beside) the test jig, to allow units to adjust to
local temperature and humidity (compare
Figure 7).
Temporary Offset in Humidity
Measurements
Sensirion Humidity & Temperature Sensors are factory-
calibrated. However, any assembly processes that
expose the sensor to high temperature, will cause a
temporary offset in the humidity value. The reflow
soldering process (that exposes the sensor to
temperatures above 240 deg C) typically causes a
temporary offset of about -1 %RH to -2 %RH. Other
processes exposing the sensor to high temperatures
may cause a similar offset.
This offset is only temporary and will slowly recover
once the sensor is exposed to ambient conditions.

Figure 17 Mean deviation from initial calibration when sensor
is exposed to ambient conditions of 25 deg C and 50 %RH
directly after reflow soldering process (representative plot)
To accelerate the recovery process the units may
undergo a so called reconditioning proce ss (refer to
application note on reconditioning). However, as the
recovery will happen naturally at ambient conditions, it
is in most cases not necessary to undergo such a
reconditioning process. The temporary offset may just
need to be considered when defining the test limits (see
below).

Note that only the humidity value may be affected by a
temporary offset while the temperature value will
remain unaffected by processes at high temperature.
Measurement System Analysis
For a capable test setup only a minor percentage of
total variation is attributed to hardware (good
repeatability) and operator (good reproducibility) and
the majority of variation is directly related to the units
under test.

Once the test jig has been built and the initial test
environment and procedures have been defined it is
highly recommended to perform a Mea surement
System Analysis (MSA) to understand the repeatability
and reproducibility of measurements. If the MSA should
reveal a low measurement capability - i.e. a lot of the
total variation is attributed to the hardware and/or the
operator procedure - the test setup /procedure should
possibly be improved.

If the MSA should reveal problems related to the
temperature measurement as well as the humidity
measurement, it is recommend ed to first address the
problems on the temperature measurement. As relative
humidity is temperature dependent, any improvement
on the temperature measurement will improve the
relative humidity measurement at the same time. Once
the MSA results for tempera ture are satisfying, any
remaining problems related to relative humidity may be
addressed.

If the MSA reveals a low reliability and reproducibility
but it is not possible to further improve the capability of
the setup, the results of the MSA at least prov ides an
understanding of the limitations and accuracy of the
setup.
Test Limits
Once an optimized test system is available, appropriate
test limits need to be defined. Test limits need to
consider all of the following main factors:
- Specified accuracy of th e reference sensor(s)
(see datasheet)
- Specified accuracy of the sensor under test
(see datasheet)
- Variation caused by the measurement system
(see section
```

## Page 8

```text
www.sensirion.com / D1 Version 3 - October 2022 8/10

Measurement System Analysis)
- For humidity only: any temporary offset that is
still present when testing the sensors
(see section Temporary Offset in Humidity
Measurements)
Initial test limits should be verified using a Process
Capability (Cpk) Analysis. Once the initial test limits are
defined, the production performance should be
reviewed regularly and limits adjusted as needed.

In case units get repeatedly tested throughout the
production flow (e.g. 1 st test on PCB, 2 nd test in sub -
assembly, 3 rd test of final product) , test limits at each
stage should be more relaxed than for the previous test
stage (guard banding of limits). The larger the variation
caused by the test setup (see section
Measurement System Analysis ), the larger th is
relaxation of subsequent limits should be. This is to
prevent that units that marginally passed one test would
marginally fail a subsequent test just because of
variation caused by the test setup.
Disclaimer
The above given restrictions, recommendations,
materials, etc. do not cover all possible cases and
items. This document is not to be considered complete
and is subject to change without prior notice.
```

## Page 9

```text
www.sensirion.com / D1 Version 3 - October 2022 9/10

Revision History
Date Revision Changes
2013-07-17 1 initial revision
2013-10-01 2 Added self-heating unit with housing.
2022-10-17 3 Confidentiality change from D3 to D1, removed water mark, adapted
document to SHT85, updated disclaimer note
```

## Page 10

```text
www.sensirion.com / D1 Version 3 - October 2022 10/10

Important Notices
Warning, Personal Injury
Do not use this product as safety or emergency stop devices or in any other application where failure of the product could re sult in
personal injury. Do not use this product for applications other than its intended and authorized use. Before installing, handling, using or
servicing this product, please consult the data sheet and application notes. Failure to comply with these instructions could result in death
or serious injury.
If the Buyer shall purchase or use SENSIRION products for any unintended or unauthorized application, Buyer shall defend, indemnify and hold
harmless SENSIRION and its officers, employees, subsidiaries, affiliates and distributors against all claims, costs, damages and expenses, and
reasonable attorney fees arising out of, directly or indirectly, any claim of personal injury or death associated with such unintended or unauthorized
use, even if SENSIRION shall be allegedly negligent with respect to the design or the manufacture of the product.
ESD Precautions
The inher ent design of this component causes it to be sensitive to electrostatic discharge (ESD). To prevent ESD -induced damage and/or
degradation, take customary and statutory ESD precautions when handling this product. See application note "ESD, Latchup and EMC" for more
information.
Warranty
SENSIRION warrants solely to the original purchaser of this product for a period of 12 months (one year) from the date of del ivery that this product
shall be of the quality, material and workmanship defined in SENSIRION's pub lished specifications of the product. Within such period, if proven to
be defective, SENSIRION shall repair and/or replace this product, in SENSIRION's discretion, free of charge to the Buyer, provided that:
- notice in writing describing the defects shall be given to SENSIRION within fourteen (14) days after their appearance;
- such defects shall be found, to SENSIRION's reasonable satisfaction, to have arisen from SENSIRION's faulty design, material, or
workmanship;
- the defective product shall be returned to SENSIRION's factory at the Buyer's expense; and
- the warranty period for any repaired or replaced product shall be limited to the unexpired portion of the original period.
This warranty does not apply to any equipment which has not been installed and used within the specifications recommended by SENSIRION for
the intended and proper use of the equipment. EXCEPT FOR THE WARRANTIES EXPRESSLY SET FORTH HEREIN, SENSIRION MAKES N O
WARRANTIES, EITHER EXPRESS OR IMPLIED, WITH RESPECT TO THE PRODUCT. ANY AND ALL WARRANTIES, INCLUDING WITHOUT
LIMITATION, WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE EXPRESSLY EXCLUDED AND
DECLINED.
SENSIRION is only liable for defects of this product arising under the conditions of operation provided for in the data sheet and proper use of the
goods. SENSIRION explicitly disclaims all warranties, express or implied, for any period during which the goods are operated or stored n ot in
accordance with the technical specifications.
SENSIRION does not assume any liability arising out of any application or use of any product or circuit and specifically disclaims any and all liability,
including without limitation consequential or inci dental damages. All operating parameters, including without limitation recommended parameters,
must be validated for each customer's applications by customer's technical experts. Recommended parameters can and do vary in different
applications.
SENSIRION reserves the right, without further notice, (i) to change the product specifications and/or the information in this document a nd (ii) to
improve reliability, functions and design of this product.
Copyright (c) 2022, by SENSIRION. CMOSens(R) is a trademark of Sensirion. All rights reserved
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
