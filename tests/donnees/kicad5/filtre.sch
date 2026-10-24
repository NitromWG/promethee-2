EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 2
Title "Filtre d'essai"
Date "2026-10-25"
Rev "A"
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L Connector_Generic:Conn_01x04 J1
U 1 1 5F8E7A01
P 2000 3000
F 0 "J1" H 1918 3317 50  0000 C CNN
F 1 "Conn_01x04" H 1918 3226 50  0000 C CNN
F 2 "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical" H 2000 3000 50  0001 C CNN
F 3 "~" H 2000 3000 50  0001 C CNN
	1    2000 3000
	-1   0    0    -1  
$EndComp
Wire Wire Line
	2200 2900 2600 2900
Text Label 2350 2900 0    50   ~ 0
IN
$Comp
L Device:R R1
U 1 1 5F8E7A02
P 2750 2900
F 0 "R1" V 2543 2900 50  0000 C CNN
F 1 "10k" V 2634 2900 50  0000 C CNN
F 2 "Resistor_SMD:R_0805_2012Metric" V 2680 2900 50  0001 C CNN
F 3 "~" H 2750 2900 50  0001 C CNN
	1    2750 2900
	0    1    1    0   
$EndComp
Wire Wire Line
	2900 2900 3300 2900
$Sheet
S 3300 2700 1000 600 
U 5F8E7A10
F0 "Etage" 50
F1 "etage.sch" 50
F2 "E" I L 3300 2900 50 
F3 "S" O R 4300 2900 50 
$EndSheet
Wire Wire Line
	4300 2900 4700 2900
Text GLabel 4700 2900 2    50   Output ~ 0
OUT
Wire Wire Line
	2200 3000 2400 3000
$Comp
L power:GND #PWR01
U 1 1 5F8E7A04
P 2400 3000
F 0 "#PWR01" H 2650 3000 50  0001 C CNN
F 1 "GND" H 2550 3000 50  0000 C CNN
F 2 "" H 2400 3000 50  0001 C CNN
F 3 "" H 2400 3000 50  0001 C CNN
	1    2400 3000
	0    -1   -1   0   
$EndComp
Wire Wire Line
	2200 3100 2400 3100
$Comp
L power:VCC #PWR02
U 1 1 5F8E7A06
P 2400 3100
F 0 "#PWR02" H 2250 3100 50  0001 C CNN
F 1 "VCC" H 2550 3100 50  0000 C CNN
F 2 "" H 2400 3100 50  0001 C CNN
F 3 "" H 2400 3100 50  0001 C CNN
	1    2400 3100
	0    1    1    0   
$EndComp
Wire Wire Line
	2200 3200 2500 3200
Text Label 2500 3200 0    50   ~ 0
INV
$Comp
L projet:INV U1
U 1 1 5F8E7A03
P 5200 3500
F 0 "U1" H 5200 3817 50  0000 C CNN
F 1 "74HC14" H 5200 3726 50  0000 C CNN
F 2 "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm" H 5200 3500 50  0001 C CNN
F 3 "" H 5200 3500 50  0001 C CNN
	1    5200 3500
	-1   0    0    -1  
$EndComp
Wire Wire Line
	5500 3500 5800 3500
Text GLabel 5800 3500 2    50   Input ~ 0
OUT
Wire Wire Line
	4900 3500 4600 3500
Text Label 4600 3500 2    50   ~ 0
INV
$EndSCHEMATC
