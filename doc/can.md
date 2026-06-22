![](_page_0_Picture_0.jpeg)

# 5.7 kV rms, Signal Isolated, High Working Voltage, CAN FD Transceiver with ±15kV IEC ESD

Data Sheet [ADM3058E](https://www.analog.com/adm3058e?doc=adm3058E.pdf)

## <span id="page-0-0"></span>**FEATURES**

**5.7 kV rms signal isolated CAN FD transceiver 1500 V peak and dc working voltage to DIN VDE 0884-11 1.7 V to 5.5 V supply and logic side levels 4.5 V to 5.5 V supply on bus side ISO 11898-2:2016-compliant CAN FD Data rates up to 12 Mbps for CAN FD Low maximum loop propagation delay: 155 ns Extended common-mode range: ±25 V Bus fault protection (CANH, CANL): ±40 V ESD protection on the CANH and CANL bus pins** 

**≥±8 kV IEC 61000-4-2 contact discharge ≥±15 kV IEC 61000-4-2 air discharge Passes EN 55022, Class B by 6 dB** 

**[Safety and regulatory approvals \(](https://www.analog.com/icouplersafety?doc=ADM3058E.pdf)pending)** 

**UL: 5700 V rms for 1-minute per UL 1577 CSA Component Acceptance 5A at 5.7 kV rms IEC 60601 and IEC 61010 VDE Certificates of Conformity DIN VDE V 0884-11 (VDE V 0884-11):2017-01 VIORM = 1500 V peak** 

<span id="page-0-1"></span>**High common-mode transient immunity: >50 kV/μs Industrial operating temperature range: −40°C to +125°C** 

### **APPLICATIONS**

<span id="page-0-3"></span>**CANOpen, DeviceNet, and other CAN bus implementations Industrial automation Process control and building control Transport and infrastructure** 

## **GENERAL DESCRIPTION**

The ADM3058E is a 5.7 kV rms isolated controller area network (CAN) physical layer transceiver with a high performance, basic feature set. The ADM3058E fully meets the CAN flexible data rate (CAN FD) ISO 11898-2:2016 requirements and is further capable of supporting data rates as high as 12 Mbps.

The device employs Analog Devices, Inc., *i*Coupler® technology to combine a 2-channel isolator and a CAN transceiver into a single small outline integrated circuit (SOIC) surface-mount package. The ADM3058E is a fully isolated solution for CAN and CAN FD applications. The ADM3058E provides isolation between the CAN controller and physical layer bus. Safety and

## **FUNCTIONAL BLOCK DIAGRAM**

<span id="page-0-2"></span>![](_page_0_Figure_15.jpeg)

regulatory approvals (pending) for a 5.7 kV rms withstand voltage and a 1500 V peak working voltage ensure that the ADM3058E meets application isolation requirements.

Low loop propagation delays and the extended common-mode range of ±25 V support robust communication on longer bus cables. Dominant timeout functionality protects against bus lock up in a fault condition, and current limiting and thermal shutdown features protect against output short circuits. The CAN bus input and output pins are protected to ±40 V against accidental connection to a +24 V bus supply. The device is fully specified over the −40°C to +125°C industrial temperature range.

# **TABLE OF CONTENTS**

| Features1                                      |
|------------------------------------------------|
| Applications                                   |
| Functional Block Diagram                       |
| General Description                            |
| Revision History                               |
| Specifications                                 |
| Timing Specifications                          |
| Insulation and Safety Related Specifications6  |
| Package Characteristics                        |
| Regulatory Information6                        |
| DIN V VDE V 0884-11 (VDE V 0884-11) Insulation |
| Characteristics (Pending)                      |
| Absolute Maximum Ratings                       |
| Thermal Resistance                             |
| Electrostatic Discharge (ESD) Ratings8         |
| ESD Caution                                    |
| Pin Configuration and Function Descriptions9   |
| Typical Performance Characteristics            |
| Test Circuits                                  |
| Terminology                                    |
| REVISION HISTORY                               |

| Theory of Operation                                                       |
|---------------------------------------------------------------------------|
| CAN Transceiver Operation14                                               |
| Signal Isolation                                                          |
| Integrated and Certified IEC Electromagnetic Compatibility (EMC) Solution |
| ±40 V Miswire Protection                                                  |
| Dominant Timeout                                                          |
| Fail-Safe Features                                                        |
| Thermal Shutdown                                                          |
| Applications Information                                                  |
| Radiated Emissions and PCB Layout                                         |
| PCB Layout16                                                              |
| Thermal Analysis                                                          |
| Insulation Lifetime                                                       |
| Surface Tracking16                                                        |
| Insulation Wear Out                                                       |
| Calculation and Use of Parameters Example                                 |
| Outline Dimensions                                                        |
| Ordering Guide                                                            |

<span id="page-1-0"></span>**8/2020—Rev. 0 to Rev. A**Changes to Data Sheet Title and Features Section ....................................

6/2020—Revision 0: Initial Version

# <span id="page-2-0"></span>SPECIFICATIONS

All voltages are relative to their respective ground, 1.7 V ≤ VDD1 ≤ 5.5 V, 4.5 V ≤ VDD2 ≤ 5.5 V, and −40°C ≤ TA ≤ +125°C, unless otherwise noted. Typical specifications are at VDD1 = VDD2 = 5 V and TA = 25°C, unless otherwise noted.

**Table 1.** 

| Parameter                                                                 | Symbol          | Min            | Typ | Max            | Unit | Test Conditions/Comments                                                                      |
|---------------------------------------------------------------------------|-----------------|----------------|-----|----------------|------|-----------------------------------------------------------------------------------------------|
| SUPPLY CURRENT                                                            |                 |                |     |                |      |                                                                                               |
| Bus Side                                                                  | IDD2            |                |     |                |      |                                                                                               |
| Recessive State                                                           |                 |                | 5.3 | 7              | mA   | TXD high, load resistance (RL) = 60 Ω                                                         |
| Dominant State                                                            |                 |                | 63  | 75             | mA   | Limited by transmit dominant timeout (tDT),<br>see the Theory of Operation section, RL = 60 Ω |
| 70% Dominant/30% Recessive                                                |                 |                |     |                |      | Worst case, see the Theory of Operation section,<br>RL = 60 Ω                                 |
| 1 Mbps                                                                    |                 |                | 45  | 58             | mA   |                                                                                               |
| 5 Mbps                                                                    |                 |                | 49  | 60             | mA   |                                                                                               |
| 12 Mbps                                                                   |                 |                | 58  | 65             | mA   |                                                                                               |
| Logic Side iCoupler Current                                               | IDD1            |                |     | 5.5            | mA   | TXD high, low, or switching                                                                   |
| DRIVER                                                                    |                 |                |     |                |      |                                                                                               |
| Differential Outputs                                                      |                 |                |     |                |      | See Figure 17                                                                                 |
| Recessive State Voltage                                                   |                 |                |     |                |      | TXD high, RL, and common-mode filter<br>capacitor (CF) open                                   |
| CANH, CANL                                                                | VCANL,<br>VCANH | 2.0            |     | 3.0            | V    |                                                                                               |
| Differential Output                                                       | VOD             | −500           |     | +50            | mV   |                                                                                               |
| Dominant State Voltage                                                    |                 |                |     |                |      | TXD low, CF open                                                                              |
| CANH                                                                      | VCANH           | 2.75           |     | 4.5            | V    | 50 Ω ≤ RL ≤ 65 Ω                                                                              |
| CANL                                                                      | VCANL           | 0.5            |     | 2.0            | V    | 50 Ω ≤ RL ≤ 65 Ω                                                                              |
| Differential Output                                                       | VOD             | 1.5            |     | 3.0            | V    | 50 Ω ≤ RL ≤ 65 Ω                                                                              |
|                                                                           |                 | 1.4            |     | 3.3            | V    | 45 Ω ≤ RL ≤ 70 Ω                                                                              |
|                                                                           |                 | 1.5            |     | 5.0            | V    | RL = 2240 Ω                                                                                   |
| Output Symmetry (VDD2 − VCANH to<br>VCANL)                                | VSYM            | −0.55          |     | +0.55          | V    | RL = 60 Ω, CF = 4.7 nF                                                                        |
| Short-Circuit Current                                                     | ISC             |                |     |                |      | RL open                                                                                       |
| Absolute                                                                  |                 |                |     |                |      |                                                                                               |
| CANH                                                                      |                 |                |     | 115            | mA   | VCANH = −3 V                                                                                  |
| CANL                                                                      |                 |                |     | 115            | mA   | VCANL = 18 V                                                                                  |
| Steady State                                                              |                 |                |     |                |      |                                                                                               |
| CANH                                                                      |                 |                |     | 115            | mA   | VCANH = −24 V                                                                                 |
| CANL                                                                      |                 |                |     | 115            | mA   | VCANL = 24 V                                                                                  |
| Logic Input TXD                                                           |                 |                |     |                |      |                                                                                               |
| Input Voltage                                                             |                 |                |     |                |      |                                                                                               |
| High                                                                      | VIH             | 0.65 ×<br>VDD1 |     |                | V    |                                                                                               |
| Low                                                                       | VIL             |                |     | 0.35 ×<br>VDD1 | V    |                                                                                               |
| Complementary Metal-Oxide<br>Semiconductor (CMOS) Logic Input<br>Currents | IIH ,  IIL      |                |     | 10             | μA   | Input high or low                                                                             |
| RECEIVER                                                                  |                 |                |     |                |      |                                                                                               |
| Differential Inputs                                                       |                 |                |     |                |      |                                                                                               |
| Differential Input Voltage Range                                          | VID             |                |     |                |      | See Figure 18, RXD capacitance (CRXD) open,<br>−25 V < VCANL, VCANH < +25 V                   |
| Recessive                                                                 |                 | −1.0           |     | +0.5           | V    |                                                                                               |
|                                                                           |                 |                |     |                |      |                                                                                               |

| Parameter                       | Symbol     | Min    | Typ | Max   | Unit  | Test Conditions/Comments                      |
|---------------------------------|------------|--------|-----|-------|-------|-----------------------------------------------|
| Input Voltage Hysteresis        | VHYS       |        | 150 |       | mV    |                                               |
| Unpowered Input Leakage Current | IIN (OFF)  |        |     | 10    | μA    | VCANH, VCANL = 5 V, VDD2 = 0 V                |
| Input Resistance                |            |        |     |       |       |                                               |
| CANH, CANL                      | RINH, RINL | 6      |     | 25    | kΩ    | See Figure 21                                 |
| Differential                    | RDIFF      | 20     |     | 100   | kΩ    | See Figure 20                                 |
| Input Resistance Matching       | mR         | −0.03  |     | +0.03 |       | mR = 2 × (RINH − RINL)/(RINH + RINL)          |
| Input Capacitance               |            |        |     |       |       |                                               |
| CANH, CANL                      | CINH, CINL |        | 35  |       | pF    | See Figure 21                                 |
| Differential                    | CDIFF      |        | 12  |       | pF    | See Figure 20                                 |
| Logic Output (RXD)              |            |        |     |       |       |                                               |
| Output Voltage                  |            |        |     |       |       |                                               |
| Low                             | VOL        |        | 0.2 | 0.4   | V     | Output impedance (IOUT) = 2 mA                |
| High                            | VOH        | VDD1 − |     |       | V     | IOUT = −2 mA                                  |
|                                 |            | 0.2    |     |       |       |                                               |
| Short-Circuit Current           | IOS        | 7      |     | 85    | mA    | Output voltage (VOUT) = GND1 or VDD1          |
| COMMON-MODE TRANSIENT IMMUNITY1 |            |        |     |       |       | Common-mode voltage (VCM) ≥ 1 kV, transient   |
|                                 |            |        |     |       |       | magnitude ≥ 800 V                             |
| Input High, Recessive           | CMH        | 50     | 100 |       | kV/μs | Input voltage (VIN) = VDD1 (TXD) or CANH/CANL |
|                                 |            |        |     |       |       | recessive                                     |
| Input Low, Dominant             | CML        | 50     | 100 |       | kV/μs | VIN = 0 V (TXD) or CANH/CANL dominant         |

 <sup>|</sup>CMH| is the maximum common-mode voltage slew rate that can be sustained while maintaining CANH/CANL recessive or RXD ≥ VDD1 − 0.2 V. |CML| is the maximum common-mode voltage slew rate that can be sustained while maintaining CANH/CANL dominant or RXD ≤ 0.4 V. The common-mode voltage slew rates apply to both rising and falling common-mode voltage edges.

## <span id="page-4-0"></span>**TIMING SPECIFICATIONS**

All voltages are relative to their respective ground, 1.7 V ≤ VDD1 ≤ 5.5 V, 4.5 V ≤ VDD2 ≤ 5.5 V, and −40°C ≤ TA ≤ +125°C, unless otherwise noted. Typical specifications are at VDD1 = VDD2 = 5 V and TA = 25°C, unless otherwise noted.

**Table 2.** 

| Parameter                         | Symbol     | Min  | Typ | Max  | Unit | Test Conditions/Comments                                      |
|-----------------------------------|------------|------|-----|------|------|---------------------------------------------------------------|
| DRIVER                            |            |      |     |      |      | See Figure 2 and Figure 17, tBIT_TXD1<br>= 200 ns, RL = 60 Ω, |
|                                   |            |      |     |      |      | load capacitance (CL) = 100 pF                                |
| Maximum Data Rate                 |            | 12   |     |      | Mbps |                                                               |
| Propagation Delay from TXD to Bus |            |      |     |      |      |                                                               |
| Recessive to Dominant             | tTXD_DOM   |      | 35  | 60   | ns   |                                                               |
| Dominant to Recessive             | tTXD_REC   |      | 45  | 70   | ns   |                                                               |
| Transmit Dominant Timeout         | tDT        | 1175 |     | 4000 | μs   | TXD low, see Figure 3                                         |
| RECEIVER                          |            |      |     |      |      | See Figure 2 and Figure 19, tBIT_TXD = 200 ns, RL = 60 Ω,     |
|                                   |            |      |     |      |      | CL = 100 pF, CRXD = 15 pF                                     |
| Loop Propagation Delay            |            |      |     |      |      |                                                               |
| Falling Edge (TXD to RXD)         | tLOOP_FALL |      |     | 155  | ns   |                                                               |
| Rising Edge (TXD to RXD)          | tLOOP_RISE |      |     | 155  | ns   |                                                               |
| Loop Delay Symmetry (Minimum      | tBIT_RXD   |      |     |      |      |                                                               |
| Recessive Bit Width)              |            |      |     |      |      |                                                               |
| 2 Mbps                            |            | 450  |     | 550  | ns   | tBIT_TXD = 500 ns                                             |
| 5 Mbps                            |            | 160  |     | 220  | ns   | tBIT_TXD = 200 ns                                             |
| 8 Mbps                            |            | 85   |     | 140  | ns   | tBIT_TXD = 125 ns                                             |
| 12 Mbps                           |            | 50   |     | 91.6 | ns   | tBIT_TXD = 83.3 ns                                            |

tBIT\_TXD is the bit time at the TXD pin as transmitted by the CAN controller.

### **Timing Diagrams**

![](_page_4_Figure_7.jpeg)

Figure 2. Transceiver Timing Diagram

<span id="page-4-2"></span><span id="page-4-1"></span>![](_page_4_Figure_9.jpeg)

Figure 3. Dominant Timeout, tDT

#### <span id="page-5-0"></span>INSULATION AND SAFETY RELATED SPECIFICATIONS

For additional information, see www.analog.com/icouplersafety.

<span id="page-5-4"></span>Table 3.

| Parameter                                                                      | Symbol  | Value | Unit   | Test Conditions/Comments                                                                                                   |
|--------------------------------------------------------------------------------|---------|-------|--------|----------------------------------------------------------------------------------------------------------------------------|
| Rated Dielectric Insulation Voltage                                            |         | 5700  | V rms  | 1-minute duration                                                                                                          |
| Minimum External Air Gap (Clearance)                                           | L (I01) | 8.3   | mm min | Measured from input terminals to output terminals, shortest distance through air                                           |
| Minimum External Tracking (Creepage)                                           | L (102) | 8.3   | mm min | Measured from input terminals to output terminals, shortest distance path along body                                       |
| Minimum Clearance in the Plane of the<br>Printed Circuit Board (PCB) Clearance | L (PCB) | 8.3   | mm min | Measured from input terminals to output terminals, shortest distance through air, line of sight, in the PCB mounting plane |
| Minimum Internal Gap (Internal<br>Clearance)                                   |         | 40    | μm min | Insulation distance through insulation                                                                                     |
| Tracking Resistance (Comparative<br>Tracking Index)                            | СТІ     | >600  | V      | DIN IEC 112/VDE 0303 Part 1                                                                                                |
| Material Group                                                                 |         | I     |        | Material group (DIN VDE 0110, 1/89, Table 1)                                                                               |

#### <span id="page-5-1"></span>**PACKAGE CHARACTERISTICS**

Table 4.

| Parameter                                  | Symbol           | Min Ty | o Max | Unit | Test Conditions/Comments |
|--------------------------------------------|------------------|--------|-------|------|--------------------------|
| Resistance (Input to Output) <sup>1</sup>  | R <sub>I-O</sub> | 10     | 3     | Ω    |                          |
| Capacitance (Input to Output) <sup>1</sup> | C <sub>I-O</sub> | 0.9    |       | pF   | f = 1 MHz                |
| Input Capacitance <sup>2</sup>             | Cı               | 4.0    |       | pF   |                          |

<sup>&</sup>lt;sup>1</sup> The device is considered a 2-terminal device: Pin 1 through Pin 4 are shorted together, and Pin 5 through Pin 8 are shorted together.

#### <span id="page-5-2"></span>**REGULATORY INFORMATION**

See Table 10 and the Insulation Lifetime section for the recommended maximum working voltages for specific cross isolation waveforms and insulation levels. The ADM3058E is pending approval by the organizations listed in Table 5.

<span id="page-5-3"></span>Table 5.

| UL (Pending)                                         | CSA (Pending)                                                                     | VDE (Pending)                                                     | CQC (Pending)                                      |
|------------------------------------------------------|-----------------------------------------------------------------------------------|-------------------------------------------------------------------|----------------------------------------------------|
| UL1577 Component<br>Recognition Program <sup>1</sup> | Approved under CSA Component Acceptance Notice 5A                                 | DIN V VDE V 0884-11<br>(VDE V 0884-11):2017-01 <sup>2</sup>       | Certified under CQC11-<br>471543-2012              |
| Single Protection, 5.7 kV rms<br>Isolation Voltage   | CSA 60950-1-07+A1+A2 and IEC 60950-1, second edition, +A1+A2:                     | Reinforced insulation,<br>1500 V peak, V <sub>IOTM</sub> = 8000 V | GB4943.1-2011                                      |
|                                                      | Basic insulation at 830 V rms (1137 V peak)                                       |                                                                   | Basic insulation at 830 V rms<br>(1137 V peak)     |
|                                                      | Reinforced insulation at 415 V rms<br>(587 V peak)                                |                                                                   | Reinforced insulation at<br>415 V rms (587 V peak) |
|                                                      | IEC 60601-1 Edition 3.1:                                                          |                                                                   |                                                    |
|                                                      | Reinforced insulation (2 means of patient protection (MOPP)), 261V rms (291 V dc) |                                                                   |                                                    |
|                                                      | CSA 61010-1-12 and IEC 61010-1 third edition:                                     |                                                                   |                                                    |
|                                                      | Basic insulation at: 300 V rms mains, 830 V secondary (1174 V peak)               |                                                                   |                                                    |
|                                                      | Reinforced insulation at: 300 V rms mains,<br>415 V secondary (587 V peak)        |                                                                   |                                                    |
| File E214100                                         | File 205078                                                                       | File 2471900-4880-0001                                            | File (pending)                                     |

<sup>&</sup>lt;sup>1</sup> In accordance with UL 1577, each ADM3058E is proof tested by applying an insulation test voltage ≥ 6840 V rms for 1 sec.

<sup>&</sup>lt;sup>2</sup> Input capacitance is from any input data pin to ground.

<sup>&</sup>lt;sup>2</sup> In accordance with DIN V VDE V 0884-11, each product is proof tested by applying an insulation test voltage ≥ 2813 V peak for 1 sec (partial discharge detection limit = 5 pC). The \* marking branded on the component designates DIN V VDE V 0884-11 approval.

## <span id="page-6-0"></span>**DIN V VDE V 0884-11 (VDE V 0884-11) INSULATION CHARACTERISTICS (PENDING)**

These isolators are suitable for reinforced electrical isolation only within the safety limit data. Protective circuits ensure the maintenance of the safety data.

**Table 6. ADM3058E VDE Characteristics** 

| Description                                                 | Test Conditions/Comments                                                                                                                                                                                                    | Symbol   | Characteristic | Unit   |
|-------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|----------|----------------|--------|
| Installation Classification per DIN VDE 0110                |                                                                                                                                                                                                                             |          |                |        |
| For Rated Mains Voltage ≤ 150 V rms                         |                                                                                                                                                                                                                             |          | I to IV        |        |
| For Rated Mains Voltage ≤ 300 V rms                         |                                                                                                                                                                                                                             |          | I to IV        |        |
| For Rated Mains Voltage ≤ 600 V rms                         |                                                                                                                                                                                                                             |          | I to IV        |        |
| Climatic Classification                                     |                                                                                                                                                                                                                             |          | 40/125/21      |        |
| Pollution Degree per DIN VDE 0110, Table<br>1               |                                                                                                                                                                                                                             |          | 2              |        |
| Maximum Working Insulation Voltage                          |                                                                                                                                                                                                                             |          |                |        |
| Reinforced                                                  |                                                                                                                                                                                                                             | VIORM    | 1500           | V peak |
| Basic, DC Working Voltage                                   | See the Absolute Maximum Ratings section and Table 10<br>for the maximum continuous working voltage for ac<br>bipolar, ac unipolar, and dc voltages, basic and<br>reinforced insulation, and 50-year lifetime to 1% failure | VIORM    | 1500           | V peak |
| Input to Output Test Voltage, Method B1                     | VIORM × 1.875 = Vpd (m), 100% production test, tini = tm =<br>1 sec, partial discharge < 5 pC                                                                                                                               | Vpd (m)  | 2813           | V peak |
| Input to Output Test Voltage, Method A                      |                                                                                                                                                                                                                             | Vpd (m)  |                |        |
| After Environmental Tests Subgroup 1                        | VIORM × 1.5 = Vpd (m), tini = 60 sec, tm = 10 sec,<br>partial discharge < 5 pC                                                                                                                                              |          | 2250           | V peak |
| After Input and/or Safety Test<br>Subgroup 2 and Subgroup 3 | VIORM × 1.2 = Vpd (m), tini = 60 sec, tm = 10 sec,<br>partial discharge < 5 pC                                                                                                                                              |          | 1800           | V peak |
| Highest Allowable Overvoltage                               |                                                                                                                                                                                                                             | VIOTM    | 8000           | V peak |
| Impulse                                                     | 1.2 μs rise time, 50 μs, 50% fall time in air to the<br>preferred sequence                                                                                                                                                  | VIMPULSE | 8000           | V peak |
| Surge Isolation Test Voltage                                | Peak voltage (VPEAK) = 12.8 kV, 1.2 μs rise time, 50 μs, and<br>50% fall time                                                                                                                                               | VIOSM    | 8000           | V peak |
| Safety Limiting Values                                      | Maximum value allowed in the event of a failure (see<br>Figure 4)                                                                                                                                                           |          |                |        |
| Maximum Junction Temperature                                |                                                                                                                                                                                                                             | TS       | 150            | °C     |
| Total Power Dissipation at 25°C                             |                                                                                                                                                                                                                             | PS       | 1.28           | W      |
| Insulation Resistance at TS                                 | Test voltage = 500 V                                                                                                                                                                                                        | RS       | >109           | Ω      |

![](_page_6_Figure_5.jpeg)

<span id="page-6-1"></span>Figure 4. Thermal Derating Curve, Dependence of Safety Limiting Values with Ambient Temperature per DIN V VDE V 0884-11 (See the [Thermal](#page-7-1)  [Resistance Section f](#page-7-1)or Additional Information)

## <span id="page-7-0"></span>**ABSOLUTE MAXIMUM RATINGS**

Pin voltages with respect to  $GND_1/GND_2$  are on same side, unless otherwise noted.

Table 7.

| Parameter                             | Rating                                      |  |
|---------------------------------------|---------------------------------------------|--|
| V <sub>DD1</sub> , V <sub>DD2</sub>   | −0.5 V to +6 V                              |  |
| Logic Side Input and Output: TXD, RXD | $-0.5 \text{ V to V}_{DD1} + 0.5 \text{ V}$ |  |
| CANH, CANL                            | -40 V to +40 V                              |  |
| Temperature                           |                                             |  |
| Industrial Operating Range            | -40°C to +125°C                             |  |
| Storage Range                         | −65°C to +150°C                             |  |
| Maximum Junction (T <sub>J</sub> )    | 150°C                                       |  |
| Moisture Sensitivity Level (MSL)      | 3                                           |  |

Stresses at or above those listed under Absolute Maximum Ratings may cause permanent damage to the product. This is a stress rating only; functional operation of the product at these or any other conditions above those indicated in the operational section of this specification is not implied. Operation beyond the maximum operating conditions for extended periods may affect product reliability.

#### <span id="page-7-1"></span>THERMAL RESISTANCE

Thermal performance is directly linked to PCB design and operating environment. Careful attention to PCB thermal design is required.

 $\theta_{JA}$  is the natural junction to ambient thermal resistance measured in a one cubic foot sealed enclosure.

<span id="page-7-5"></span>**Table 8. Thermal Resistance** 

| Package Type <sup>1</sup> | θ <sub>JA</sub> | Unit |
|---------------------------|-----------------|------|
| RI-8-1                    | 97              | °C/W |

<sup>&</sup>lt;sup>1</sup> The thermocouple is located at the center of the package underside, and the test was conducted on a 4-layer board with thin traces. See the Thermal Analysis section for the thermal model definitions.

## <span id="page-7-2"></span>**ELECTROSTATIC DISCHARGE (ESD) RATINGS**

The following ESD information is provided for handling of ESD sensitive devices in an ESD protected area only.

Human body model (HBM) per ANSI/ESDA/JEDEC JS-001.

International Electrotechnical Commission (IEC) electromagnetic compatibility: Part 4-2 (IEC) per IEC 61000-4-2.

#### **ESD Ratings for ADM3058E**

Table 9. ADM3058E, 8-Lead SOIC IC

| ESD Model        | Withstand Threshold (V)                                               | Class   |  |  |
|------------------|-----------------------------------------------------------------------|---------|--|--|
| HBM <sup>1</sup> | ±4 kV                                                                 | 3A      |  |  |
| IEC <sup>2</sup> | $\pm 8$ kV (across isolation barrier with respect to GND <sub>1</sub> | Level 4 |  |  |
| IEC <sup>3</sup> | ±8 kV (contact discharge with respect to GND <sub>2</sub> )           | Level 4 |  |  |
|                  | ±15 kV (air discharge with respect to GND <sub>2</sub>                | Level 4 |  |  |

 $<sup>^{1}</sup>$  All pins, 1.5 k $\Omega$ , 100 pF.

#### <span id="page-7-3"></span>**ESD CAUTION**

![](_page_7_Picture_23.jpeg)

**ESD (electrostatic discharge) sensitive device.**Charged devices and circuit boards can discharge without detection. Although this product features patented or proprietary protection circuitry, damage may occur on devices subjected to high energy ESD. Therefore, proper ESD precautions should be taken to avoid performance degradation or loss of functionality.

<span id="page-7-4"></span>Table 10. Maximum Continuous Working Voltage<sup>1</sup>

| Parameter             | Insulation Rating (20-Year Lifetime) <sup>2</sup> | VDE 0884-11 Lifetime Conditions Fulfilled               |  |
|-----------------------|---------------------------------------------------|---------------------------------------------------------|--|
| AC Voltage            |                                                   |                                                         |  |
| Bipolar Waveform      |                                                   |                                                         |  |
| Reinforced Insulation | 1060 V peak                                       | Lifetime limited by insulation lifetime per VDE-0884-11 |  |
| DC Voltage            |                                                   |                                                         |  |
| Basic Insulation      | 1660 V peak                                       | Lifetime limited by package creepage per IEC 60664-1    |  |
| Reinforced Insulation | 830 V peak                                        | Lifetime limited by package creepage per IEC 60664-1    |  |

<sup>&</sup>lt;sup>1</sup> The maximum continuous working voltage refers to the continuous voltage magnitude imposed across the isolation barrier. See the Insulation Lifetime section for more details.

<sup>&</sup>lt;sup>2</sup> Across the isolation barrier, GND<sub>2</sub> to GND<sub>1</sub>.

<sup>&</sup>lt;sup>3</sup> CANH/CANL.

<sup>&</sup>lt;sup>2</sup> Insulation capability without regard to creepage limitations. Working voltage may be limited by the PCB creepage when considering rms voltages for components soldered to a PCB (assumes Material Group I up to 1250 V rms), or by the SOIC\_IC package creepage of 8.3 mm, when considering rms voltages for Material Group II.

# <span id="page-8-0"></span>PIN CONFIGURATION AND FUNCTION DESCRIPTIONS

![](_page_8_Figure_2.jpeg)

Figure 5. Pin Configuration

**Table 11. Pin Function Descriptions** 

| Pin No. | Mnemonic | Description                                                                            |
|---------|----------|----------------------------------------------------------------------------------------|
| 1       | VDD1     | Power Supply, Logic Side, 1.7 V to 5.5 V. VDD1 requires a 0.1 μF decoupling capacitor. |
| 2       | TXD      | Driver Input Data.                                                                     |
| 3       | RXD      | Receiver Output Data.                                                                  |
| 4       | GND1     | Ground, Logic Side.                                                                    |
| 5       | GND2     | Ground, Bus Side.                                                                      |
| 6       | CANL     | CAN Low Input and Output.                                                              |
| 7       | CANH     | CAN High Input and Output.                                                             |
| 8       | VDD2     | Power Supply, Bus Side, 4.5 V to 5.5 V. VDD2 requires a 0.1 μF decoupling capacitor.   |

**Table 12. Operational Truth Table** 

| VDD1 | VDD2 | TXD        | Mode            | RXD           | CANH or CANL              |  |  |
|------|------|------------|-----------------|---------------|---------------------------|--|--|
| On   | On   | Low        | Normal          | Low           | Dominant (limited by tDT) |  |  |
| On   | On   | High       | Normal          | High per bus  | Recessive and set by bus  |  |  |
| Off  | On   | Don't care | Normal          | Indeterminate | Recessive and set by bus  |  |  |
| On   | Off  | Don't care | Transceiver off | High          | High-Z                    |  |  |

## <span id="page-9-0"></span>TYPICAL PERFORMANCE CHARACTERISTICS

![](_page_9_Figure_2.jpeg)

Figure 6. Supply Current (IDD2) vs. Data Rate

![](_page_9_Figure_4.jpeg)

Figure 7. Receiver Input Hysteresis vs. Temperature

![](_page_9_Figure_6.jpeg)

Figure 8. t<sub>TXD\_DOM</sub> vs. Temperature

![](_page_9_Figure_8.jpeg)

Figure 9.  $t_{TXD\_REC}$  vs. Temperature

![](_page_9_Figure_10.jpeg)

Figure 10. tLOOP\_RISE vs. Temperature

![](_page_9_Figure_12.jpeg)

Figure 11. tLOOP\_FALL vs. Temperature

![](_page_10_Figure_1.jpeg)

Figure 12. Differential Output Voltage vs. Temperature, RL = 60 Ω

![](_page_10_Figure_3.jpeg)

Figure 13. Differential Output Voltage vs. Supply Voltage (VDD2), RL = 60 Ω

![](_page_10_Figure_5.jpeg)

Figure 14. Supply Current (IDD1) vs. Temperature

![](_page_10_Figure_7.jpeg)

Figure 15. Supply Current (IDD2) vs. Temperature

![](_page_10_Figure_9.jpeg)

Figure 16. Dominant Timeout (tDT) vs. Temperature

20135-018

20135-020

# <span id="page-11-0"></span>TEST CIRCUITS

![](_page_11_Picture_2.jpeg)

Figure 17. Driver Voltage Measurement

<span id="page-11-1"></span>![](_page_11_Picture_4.jpeg)

Figure 18. Receiver Voltage Measurement

<span id="page-11-2"></span>![](_page_11_Picture_6.jpeg)

**1. 1% TOLERANCE FOR ALL RESISTORS AND CAPACITORS.**

<span id="page-11-5"></span>Figure 19. Switching Characteristics Measurements

![](_page_11_Picture_9.jpeg)

Figure 20. RDIFF and CDIFF Measured in Recessive State, Bus Disconnected

<span id="page-11-4"></span><span id="page-11-3"></span>![](_page_11_Picture_11.jpeg)

Figure 21. Input Resistance (RINx) and Input Capacitance (CINx) Measured in Recessive State, Bus Disconnected

# <span id="page-12-0"></span>TERMINOLOGY

#### **IDD1**

IDD1 is the current drawn by the VDD1 pin.

#### **IDD2**

IDD2 is the current drawn by the VDD2 pin.

### **VOD and VID**

VOD and VID are the differential voltages from the transmitter or at the receiver on the CANH and CANL pins.

#### **tTXD\_DOM**

tTXD\_DOM is the propagation delay from a low signal on TXD to transition the bus to a dominant state.

#### **tTXD\_REC**

tTXD\_REC is the propagation delay from a high signal on TXD to transition the bus to a recessive state.

#### **tLOOP\_FALL**

tLOOP\_FALL is the propagation delay of a low signal on the TXD pin to the bus dominant. tLOOP\_FALL transitions low on the RXD pin.

#### **tLOOP\_RISE**

tLOOP\_RISE is the propagation delay of a high signal on TXD to the bus recessive. tLOOP\_RISE transitions high on the RXD pin.

#### **tBIT\_TXD**

tBIT\_TXD is the bit time at the TXD pin as transmitted by the CAN controller. Se[e Figure 2](#page-4-1) for level definitions.

#### **tBIT\_BUS**

tBIT\_BUS is the bit time as transmitted by the transceiver to the bus. When compared with a given tBIT\_TXD, a measure of bit symmetry from the TXD digital isolation channel and CAN transceiver can be determined. Se[e Figure 2 f](#page-4-1)or level definitions.

#### **tBIT\_RXD**

tBIT\_RXD is the bit time on the RXD output pin, which can be compared with tBIT\_TXD for a round trip measure of pulse width distortion through the TXD digital isolation channel, the CAN transceiver, and back through the RXD isolation channel.

## <span id="page-13-1"></span><span id="page-13-0"></span>THEORY OF OPERATION **CAN TRANSCEIVER OPERATION**

The ADM3058E facilitates communication between a CAN controller and the CAN bus. The CAN controller and the ADM3058E communicate with standard 1.8 V, 2.5 V, 3.3 V, or 5.0 V CMOS levels. The internal transceiver translates the CMOS levels to and from the CAN bus.

The CAN bus has two states: dominant and recessive. The recessive state is present on the bus when the differential voltage between CANH and CANL is less than 0.5 V. In the recessive state, both the CANH pin and CANL pin are set to high impedance and are loosely biased to a single-ended voltage of 2.5 V. A dominant state is present on the bus when the differential voltage between CANH and CANL is greater than 1.5 V. The transceiver transmits a dominant state by driving the single-ended voltage of the CANH line to 3.5 V and the CANL pin to 1.5 V. The recessive and dominant states correspond to CMOS high and CMOS low, respectively, on the RXD pin and TXD pin.

A dominant state from another node overwrites a recessive state on the bus. A CAN frame can be set for higher priority by using a longer string of dominant bits to gain control of the CAN bus during the arbitration phase. While transmitting, a CAN transceiver also reads back the state of the bus. When a CAN controller receives a dominant state while transmitting a recessive state during arbitration, the CAN controller surrenders the bus to the node still transmitting the dominant state. The node that gains control during the arbitration phase reads back only its own transmission. This interaction between recessive and dominant states allows competing nodes to negotiate for control of the bus while avoiding contention between nodes.

Industrial applications can have long cable runs. These long runs may have differences in local earth potential. Different sources may also power nodes. The ADM3058E transceiver has a ±25 V common-mode range (CMR) that exceeds the ISO11898-2 requirement and further increases the tolerance to ground variation.

See the [AN-1123 Application Note f](https://www.analog.com/media/en/technical-documentation/application-notes/AN-1123.pdf?doc=ADM3058E.pdf)or additional information on CAN.

## <span id="page-13-2"></span>**SIGNAL ISOLATION**

The ADM3058E device provides galvanic signal isolation implemented on the logic side of the interface. The RXD and TXD channels are isolated using a low propagation delay on/off keying (OOK) architecture with *i*Coupler digital isolation technology.

The low propagation delay isolation, quick transceiver conversion speeds, and integrated form factor are critical for longer cable lengths, higher data speeds, and reducing the total solution board space. The ADM3058E isolated transceiver reduces solution board space while increasing data transfer rates over discrete optocoupler and transceiver solutions.

## <span id="page-13-3"></span>**INTEGRATED AND CERTIFIED IEC ELECTROMAGNETIC COMPATIBILITY (EMC) SOLUTION**

Typically, designers must add protections against harsh operating environments while also making the product as small as possible. To reduce the board space and the design efforts needed to meet system level ESD standards, the ADM3058E isolated transceiver has robust protection circuitry on chip for the CANH and CANL lines.

### <span id="page-13-4"></span>**±40 V MISWIRE PROTECTION**

High voltage miswire events commonly occur when the system power supply is connected directly to the CANH and the CANL bus lines during assembly. Supplies can also be shorted by accidental damage to the field bus cables while the system is operating. Accounting for inductive kick and switching effects, the ADM3058E isolated transceiver CAN bus lines are protected against these miswire or shorting events in systems with up to nominal 24 V supplies. The CANH and CANL signal lines can withstand a continuous supply short with respect to GND2 or between the CAN bus lines without damage. This level of protection applies when the device is either powered or unpowered.

## <span id="page-13-5"></span>**DOMINANT TIMEOUT**

The ADM3058E features a dominant timeout (tDT i[n Figure 3\)](#page-4-2). A TXD line shorted to ground or malfunctioning CAN controller are examples of how a single node can indefinitely prevent further bus traffic. tDT limits how long the dominant state can transmit to the CAN bus by the transceiver. The TXD function restores when the line is presented with a logic low.

The tDT minimum also inherently creates a minimum data rate. Under normal operation, the CAN protocol allows five consecutive bits of the same polarity before stuffing a bit of opposite polarity into the transmitting bit sequence. When an error is detected, the CAN controller purposely violates the bit stuffing rules by producing six consecutive dominant bits. At any given data rate, the CAN controller must transmit as many as 11 consecutive dominant bits to effectively limit the ADM3058E minimum data rate to 9600 bps.

### <span id="page-13-6"></span>**FAIL-SAFE FEATURES**

In cases where the TXD input pin is allowed to float to prevent bus traffic interruption, the TXD input channel has an internal pull-up to the VDD1 pin. The pull-up holds the transceiver in the recessive state.

## <span id="page-14-0"></span>**THERMAL SHUTDOWN**

The integrated transceiver is designed with thermal shutdown circuitry to protect the device from excessive power dissipation during fault conditions. Shorting the driver outputs to a low impedance source can result in high driver currents. The thermal sensing circuitry detects the increase in die temperature under this condition and disables the driver outputs. The circuitry disables the driver outputs when the die temperature reaches 175°C. The drivers are enabled after the die has cooled.

## <span id="page-15-1"></span><span id="page-15-0"></span>APPLICATIONS INFORMATION **RADIATED EMISSIONS AND PCB LAYOUT**

The ADM3058E isolated CAN transceivers with integrated dc-to-dc converters pass EN 55022, Class B by 6 dB on a simple 2-layer PCB design. Neither stitching capacitance nor high voltage surface-mount technology (SMT) safety capacitors are required to meet this emission level.

## <span id="page-15-2"></span>**PCB LAYOUT**

The ADM3058E isolated CAN transceiver requires no external interface circuitry for the logic interfaces. Power supply bypassing is required at the logic input supply (VDD1), and the shared CAN transceiver and digital isolator supply pin (VDD2). The recommended bypass capacitor value is 0.1 μF. Note that low effective series resistance (ESR) bypass capacitors are required and must be placed as close to the chip pads as possible. The total lead length between both ends of the capacitor and the input power supply pin must not exceed 10 mm.

In applications involving high common-mode transients, minimize board coupling across the isolation barrier. Design the board layout so that any coupling that does occur equally affects all pins on a given component side. Failure to ensure this equal coupling can cause voltage differences between pins exceeding the absolute maximum ratings of the device, thereby leading to latch-up or permanent damage.

![](_page_15_Picture_6.jpeg)

Figure 22. Recommended PCB Layout

### <span id="page-15-3"></span>**THERMAL ANALYSIS**

The ADM3058E device consists of three internal die attached to a split lead frame. For the purposes of thermal analysis, the die are treated as a thermal unit, with the highest junction temperature reflected in the θJA value fro[m Table 8.](#page-7-5) The θJA value is based on measurements taken with the devices mounted on a JEDEC standard, 4-layer board with fine width traces and still air.

## <span id="page-15-4"></span>**INSULATION LIFETIME**

All insulation structures eventually break down when subjected to voltage stress over a sufficiently long period of time. The rate of insulation degradation is dependent on the characteristics of the voltage waveform applied across the insulation as well as on the materials and material interfaces.

The two types of insulation degradation of primary interest are breakdown along surfaces exposed to air and insulation wear out. Surface breakdown is the phenomenon of surface tracking and is the primary determinant of surface creepage requirements in system level standards. Insulation wear out is the phenomenon where charge injection or displacement

currents inside the insulation material cause long-term insulation degradation.

## <span id="page-15-5"></span>**SURFACE TRACKING**

Surface tracking is addressed in electrical safety standards by setting a minimum surface creepage based on the working voltage, the environmental conditions, and the properties of the insulation material. Safety agencies perform characterization testing on the surface insulation of components, allowing the components to be categorized in different material groups. Lower material group ratings are more resistant to surface tracking and can therefore provide adequate lifetime with smaller creepage. The minimum creepage for a given working voltage and material group is in each system level standard and is based on the total rms voltage across the isolation, pollution degree, and material group.

The material group and creepage for the ADM3058E isolator is listed i[n Table 3](#page-5-4) for the 8-lead, wide body SOIC package.

## <span id="page-15-6"></span>**INSULATION WEAR OUT**

The lifetime of insulation caused by wear out is determined by its thickness, material properties, and the voltage stress applied. It is important to verify that the product lifetime is adequate at the application working voltage. The working voltage supported by an isolator for wear out may not be the same as the working voltage supported for tracking. The working voltage applicable to tracking is specified in most standards.

Testing and modeling have shown that the primary driver of long-term degradation is displacement current in the polyimide insulation causing incremental damage. The stress on the insulation can be broken down into broad categories, such as dc stress, which causes little wear out because there is no displacement current, and an ac component time varying voltage stress, which causes wear out.

The ratings in certification documents are usually based on 60 Hz sinusoidal stress because this reflects isolation from line voltage. Many practical applications have combinations of 60 Hz ac and dc across the barrier, as shown in Equation 1. Because only the ac portion of the stress causes wear out, the equation can be rearranged to solve for the ac rms voltage, as shown in Equation 2. For insulation wear out with the polyimide materials used in this product, the ac rms voltage determines the product lifetime.

$$V_{RMS} = \sqrt{V_{AC\,RMS}^2 + V_{DC}^2} \tag{1}$$

or

$$V_{AC\,RMS} = \sqrt{V_{RMS}^2 - V_{DC}^2} \tag{2}$$

where:

*VRMS* is the total rms working voltage.

*VAC RMS* is the time varying portion of the working voltage. *VDC* is the dc offset of the working voltage.

## <span id="page-16-0"></span>**CALCULATION AND USE OF PARAMETERS EXAMPLE**

The following example frequently arises in power conversion applications. Assume that the line voltage on one side of the isolation is 240 V ac rms and a 400 V dc bus voltage is present on the other side of the isolation barrier. The isolator material is polyimide. To establish the critical voltages in determining the creepage, clearance, and lifetime of a device, see [Figure 23](#page-16-1)  and the following equations.

![](_page_16_Figure_3.jpeg)

<span id="page-16-1"></span>Figure 23. Critical Voltage Example

The working voltage across the barrier from Equation 1 is

$$V_{RMS} = \sqrt{{V_{AC\,RMS}}^2 + {V_{DC}}^2}$$

$$V_{RMS} = \sqrt{240^2 + 400^2}$$

$$V_{RMS} = 466 \text{ V}$$

This VRMS value is the working voltage used together with the material group and pollution degree when looking up the creepage required by a system standard.

To determine if the lifetime is adequate, obtain the time varying portion of the working voltage. To obtain the ac rms voltage, VAC RMS, use Equation 2.

$$V_{AC\,RMS} = \sqrt{V_{RMS}^2 - V_{DC}^2}$$

$$V_{ACRMS} = \sqrt{466^2 - 400^2}$$

$$V_{AC\,RMS} = 240 \text{ V rms}$$

In this case, VAC RMS is simply the line voltage of 240 V rms. This calculation is more relevant when the waveform is not sinusoidal. The value is compared to the limits for working voltage i[n Table 10](#page-7-4) for the expected lifetime, which is less than a 60 Hz sine wave, and is well within the limit for a 50-year service life.

Note that the dc working voltage limit is set by the creepage of the package as specified in IEC 60664-1. This value can differ for specific system level standards.

# <span id="page-17-0"></span>OUTLINE DIMENSIONS

![](_page_17_Figure_2.jpeg)

Figure 24. 8-Lead Standard Small Outline Package, with Increased Creepage [SOIC\_IC] Wide Body (RI-8-1)

Dimensions shown in millimeters

### <span id="page-17-1"></span>**ORDERING GUIDE**

| Model1           | Temperature Range | Package Description                             | Package Option |
|------------------|-------------------|-------------------------------------------------|----------------|
| ADM3058EBRIZ     | −40°C to +125°C   | 8-Lead Standard Small Outline Package [SOIC_IC] | RI-8-1         |
| ADM3058EBRIZ-RL  | −40°C to +125°C   | 8-Lead Standard Small Outline Package [SOIC_IC] | RI-8-1         |
| EVAL-ADM3058EEBZ |                   | Evaluation Board                                |                |

Z = RoHS Compliant Part.