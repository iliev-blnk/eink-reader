"""Circuitul plăcii proprii v1: piesele, rețelele, amprentele.

Sursa unică: `gen_sch.py` face schema din el, `gen_pcb.py` face placa din el, iar
`make.sh` verifică placa față de schemă (DRC cu `--schematic-parity`). Pinii unui
simbol care nu apar în `pins` sunt marcați neconectați în schemă.

Valorile și motivele lor sunt în README.md (tabelul pieselor și bugetul de consum).
"""

PARTS = []

def part(ref, lib, value, fp, pins, lcsc='', mpn='', group=''):
    PARTS.append(dict(ref=ref, lib=lib, value=value, fp=fp, pins=pins, lcsc=lcsc, mpn=mpn, group=group))

R0603 = 'Resistor_SMD:R_0603_1608Metric'
C0603 = 'Capacitor_SMD:C_0603_1608Metric'
LED0603 = 'LED_SMD:LED_0603_1608Metric'
# coduri LCSC pentru piesele de bază 0603; toate se verifică în catalog la comandă (PCB-5)
LCSC_R = {'1k': 'C21190', '1.8k': 'C4177', '3.3k': 'C22978', '4.7k': 'C23162', '5.1k': 'C23186',
          '10k': 'C25804', '22': 'C23345', '47k': 'C25819', '100k': 'C25803', '1M': 'C22935', '16.2k': '', '36.5k': ''}
LCSC_C = {'100nF': 'C14663', '1uF': 'C15849', '4.7uF': 'C19666', '10uF': 'C19702', '22uF': '', '1nF': 'C1588'}

def R(ref, val, a, b, group, mpn=''):
    part(ref, 'Device:R', val, R0603, {'1': a, '2': b}, LCSC_R.get(val, ''), mpn or f'{val} 1% 0603', group)

def C(ref, val, a, b, group):
    part(ref, 'Device:C', val, C0603, {'1': a, '2': b}, LCSC_C.get(val, ''), f'{val} X5R/X7R 0603', group)

# ======================================================================= USB-C
G = 'usb'
part('J1', 'Connector:USB_C_Receptacle_USB2.0_16P', 'USB-C',
     'Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal',
     {'A4': 'VBUS', 'A9': 'VBUS', 'B4': 'VBUS', 'B9': 'VBUS',
      'A1': 'GND', 'A12': 'GND', 'B1': 'GND', 'B12': 'GND', 'SH': 'GND',
      'A5': 'CC1', 'B5': 'CC2', 'A6': 'USB_DP', 'B6': 'USB_DP', 'A7': 'USB_DN', 'B7': 'USB_DN'},
     '', 'GCT USB4105-GF-A', G)
R('R1', '5.1k', 'CC1', 'GND', G)
R('R2', '5.1k', 'CC2', 'GND', G)
part('U1', 'Power_Protection:USBLC6-2P6', 'USBLC6-2SC6', 'Package_TO_SOT_SMD:SOT-23-6',
     {'1': 'USB_DP', '6': 'USB_DP', '3': 'USB_DN', '4': 'USB_DN', '2': 'GND', '5': 'VBUS'},
     'C7519', 'ST USBLC6-2SC6', G)
R('R17', '100k', 'VBUS', 'VBUS_SENSE', G)
R('R18', '100k', 'VBUS_SENSE', 'GND', G)
part('TP1', 'Connector:TestPoint', 'VBUS', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': 'VBUS'}, group=G)

# ======================================================================= ÎNCĂRCĂTORUL
G = 'charger'
part('U2', 'Battery_Management:BQ24074RGT', 'BQ24074RGTR',
     'Package_DFN_QFN:VQFN-16-1EP_3x3mm_P0.5mm_EP1.6x1.6mm_ThermalVias',
     {'13': 'VBUS', '10': 'VSYS', '11': 'VSYS', '2': 'VBAT', '3': 'VBAT', '8': 'GND', '17': 'GND',
      '4': 'GND',          # ~CE: încărcarea mereu permisă
      '5': 'GND', '6': 'VSYS',   # EN2=0, EN1=1: limita de intrare USB500
      '12': 'ILIM', '16': 'ISET', '1': 'TS', '7': 'PGOOD_N', '9': 'CHG_N'},
     '', 'TI BQ24074RGTR', G)          # TMR și ITERM libere: temporizatoare și terminare implicite
C('C1', '1uF', 'VBUS', 'GND', G)
C('C2', '4.7uF', 'VSYS', 'GND', G)
C('C3', '4.7uF', 'VBAT', 'GND', G)
R('R3', '3.3k', 'ILIM', 'GND', G)
R('R4', '1.8k', 'ISET', 'GND', G)
R('R5', '10k', 'TS', 'GND', G)
R('R6', '1k', 'VSYS', 'LED_PG', G)
R('R7', '1k', 'VSYS', 'LED_CHG', G)
part('D1', 'Device:LED', 'verde', LED0603, {'2': 'LED_PG', '1': 'PGOOD_N'}, 'C72043', 'LED verde 0603', G)
part('D2', 'Device:LED', 'portocaliu', LED0603, {'2': 'LED_CHG', '1': 'CHG_N'}, 'C72038', 'LED portocaliu 0603', G)
part('TP2', 'Connector:TestPoint', 'VSYS', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': 'VSYS'}, group=G)

# ======================================================================= BATERIA
G = 'battery'
part('J2', 'Connector_Generic:Conn_01x02', 'LiPo 1S', 'Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical',
     {'1': 'BAT_J', '2': 'GND'}, 'C131337', 'JST B2B-PH-K-S', G)
part('JP1', 'Jumper:SolderJumper_2_Bridged', 'I_BAT', 'Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm',
     {'1': 'BAT_J', '2': 'VBAT'}, group=G)
part('TP3', 'Connector:TestPoint', 'BAT_J', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': 'BAT_J'}, group=G)
part('TP4', 'Connector:TestPoint', 'VBAT', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': 'VBAT'}, group=G)
part('U6', 'EinkReader:MAX17048', 'MAX17048G+T10', 'Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm',
     {'1': 'GND', '2': 'VBAT', '3': 'VBAT', '4': 'GND', '5': 'FG_ALRT', '6': 'GND',
      '7': 'I2C_SCL', '8': 'I2C_SDA', '9': 'GND'}, '', 'Analog Devices MAX17048G+T10', G)
C('C12', '100nF', 'VBAT', 'GND', G)
R('R12', '4.7k', '+3V3', 'I2C_SCL', G)
R('R13', '4.7k', '+3V3', 'I2C_SDA', G)
R('R14', '100k', '+3V3', 'FG_ALRT', G)
R('R15', '1M', 'VBAT', 'VBAT_SENSE', G)
R('R16', '1M', 'VBAT_SENSE', 'GND', G)
C('C13', '100nF', 'VBAT_SENSE', 'GND', G)

# ======================================================================= 3,3 V
G = 'regulator'
part('SW1', 'Switch:SW_SPDT', 'PORNIT/OPRIT', 'Button_Switch_SMD:SW_SPDT_PCM12',
     {'1': 'VSYS', '2': 'PWR_EN', '3': 'GND'}, 'C221841', 'C&K PCM12SMTR', G)
part('U3', 'Regulator_Switching:TPS63900', 'TPS63900DSKR',
     'Package_SON:WSON-10-1EP_2.5x2.5mm_P0.5mm_EP1.2x2mm_ThermalVias',
     {'10': 'VSYS', '1': 'PWR_EN', '2': 'GND', '3': 'CFG1', '4': 'GND', '5': 'CFG3',
      '6': '+3V3', '7': 'LX2', '9': 'LX1', '8': 'GND', '11': 'GND'}, '', 'TI TPS63900DSKR', G)
part('L1', 'Device:L', '2.2uH', 'Inductor_SMD:L_1008_2520Metric', {'1': 'LX1', '2': 'LX2'},
     '', 'Murata DFE252010F-2R2M', G)   # din tabelul 8-2 al foii TPS63900: ISAT 3,1 A
R('R8', '36.5k', 'CFG1', 'GND', G)
R('R9', '16.2k', 'CFG3', 'GND', G)
C('C4', '10uF', 'VSYS', 'GND', G)
C('C5', '22uF', '+3V3', 'GND', G)
part('TP5', 'Connector:TestPoint', '+3V3', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': '+3V3'}, group=G)
part('TP6', 'Connector:TestPoint', 'GND', 'TestPoint:TestPoint_Pad_D1.5mm', {'1': 'GND'}, group=G)

# ======================================================================= COMUTATOARELE DE SARCINĂ
G = 'loadsw'
for ref, en, out, cref, ct, rpd, cout in (('U4', 'SD_PWR_EN', '3V3_SD', 'C7', 'CT_SD', 'R10', 'C9'),
                                          ('U5', 'EPD_PWR_EN', '3V3_EPD', 'C8', 'CT_EPD', 'R11', 'C11')):
    part(ref, 'EinkReader:TPS22917DBV', 'TPS22917DBVR', 'Package_TO_SOT_SMD:SOT-23-6',
         {'1': '+3V3', '2': 'GND', '3': en, '4': ct, '5': out, '6': out}, '', 'TI TPS22917DBVR', G)
    C(cref, '1nF', ct, '+3V3', G)          # CT la VIN: ~3,6 ms de urcare, curent de pornire mic
    R(rpd, '1M', en, 'GND', G)             # ON nu are voie să plutească
    C(cout, '10uF', out, 'GND', G)
C('C10', '100nF', '3V3_SD', 'GND', G)

# ======================================================================= MCU
G = 'mcu'
ESP = {'1': 'GND', '40': 'GND', '41': 'GND', '2': '+3V3', '3': 'ESP_EN', '27': 'BOOT',
       '4': 'BTN_PREV', '5': 'BTN_NEXT', '6': 'BTN_MENU', '7': 'FG_ALRT',
       '39': 'VBAT_SENSE', '38': 'VBUS_SENSE', '9': 'I2C_SDA', '10': 'I2C_SCL',
       '8': 'SD_PWR_EN', '23': 'EPD_PWR_EN',
       # două bus-uri SPI: cardul pe pinii de pe DevKit (SPI2), panoul pe IO47/IO48 (SPI3),
       # ca fiecare să poată fi pus pe 0 când îi e oprită alimentarea (REVIEW.md, punctul 1)
       '20': 'SD_SCK', '19': 'SD_MOSI', '21': 'SD_MISO', '22': 'SD_CS',
       '24': 'EPD_SCK', '25': 'EPD_MOSI',
       '18': 'EPD_CS', '17': 'EPD_DC', '12': 'EPD_RST', '11': 'EPD_BUSY',
       '13': 'ESP_DN', '14': 'ESP_DP', '37': 'UART_TX', '36': 'UART_RX'}
part('U7', 'RF_Module:ESP32-S3-WROOM-1', 'ESP32-S3-WROOM-1-N16R8', 'RF_Module:ESP32-S3-WROOM-1', ESP,
     'C2913202', 'Espressif ESP32-S3-WROOM-1-N16R8', G)
# rezistențele serie pe D+/D−, lângă modul (ghidul Espressif, USB)
R('R25', '22', 'ESP_DP', 'USB_DP', G)
R('R26', '22', 'ESP_DN', 'USB_DN', G)
C('C14', '22uF', '+3V3', 'GND', G)
C('C15', '100nF', '+3V3', 'GND', G)
R('R19', '10k', '+3V3', 'ESP_EN', G)
C('C16', '1uF', 'ESP_EN', 'GND', G)
R('R20', '10k', '+3V3', 'BOOT', G)
part('SW5', 'Switch:SW_Push', 'BOOT', 'Button_Switch_SMD:SW_SPST_TL3342', {'1': 'BOOT', '2': 'GND'},
     'C318884', 'C&K PTS526 / TL3342', G)
part('SW6', 'Switch:SW_Push', 'RESET', 'Button_Switch_SMD:SW_SPST_TL3342', {'1': 'ESP_EN', '2': 'GND'},
     'C318884', 'C&K PTS526 / TL3342', G)
part('J5', 'Connector_Generic:Conn_01x03', 'UART0', 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
     {'1': 'GND', '2': 'UART_TX', '3': 'UART_RX'}, group=G)

# ======================================================================= BUTOANELE
G = 'buttons'
for ref, net, fn in (('SW2', 'BTN_PREV', 'ÎNAPOI'), ('SW3', 'BTN_NEXT', 'ÎNAINTE'), ('SW4', 'BTN_MENU', 'MENIU')):
    part(ref, 'Switch:SW_Push', fn, 'Button_Switch_SMD:SW_Push_1P1T-MP_NO_Horizontal_Alps_SKRTLAE010',
         {'1': net, '2': 'GND'}, 'C110293', 'Alps SKRTLAE010', G)

# ======================================================================= AFIȘAJUL
G = 'epd'
part('J3', 'Connector_Generic:Conn_01x08', 'WeAct EPD', 'Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical',
     {'1': 'EPD_BUSY', '2': 'EPD_RST', '3': 'EPD_DC', '4': 'EPD_CS', '5': 'EPD_SCK', '6': 'EPD_MOSI',
      '7': 'GND', '8': '3V3_EPD'}, group=G)

# ======================================================================= CARDUL
G = 'sd'
part('J4', 'Connector:Micro_SD_Card', 'microSD', 'Connector_Card:microSD_HC_Molex_104031-0811',
     {'1': 'SD_DAT2', '2': 'SD_CS', '3': 'SD_MOSI', '4': '3V3_SD', '5': 'SD_SCK', '6': 'GND',
      '7': 'SD_MISO', '8': 'SD_DAT1', 'SH': 'GND'}, '', 'Molex 104031-0811', G)
R('R21', '47k', '3V3_SD', 'SD_CS', G)
R('R22', '47k', '3V3_SD', 'SD_MISO', G)
R('R23', '47k', '3V3_SD', 'SD_DAT1', G)
R('R24', '47k', '3V3_SD', 'SD_DAT2', G)

# ======================================================================= MECANICE
for i in range(1, 5):
    part(f'H{i}', 'Mechanical:MountingHole', 'M2', 'MountingHole:MountingHole_2.2mm_M2', {}, group='mech')

# rețelele cu trasee late (curent de sute de mA sau noduri de comutație)
POWER_NETS = {'VBUS', 'VSYS', 'VBAT', 'BAT_J', 'LX1', 'LX2', '3V3_SD', '3V3_EPD', 'GND', '+3V3'}
# rețelele care sunt planuri interioare: pinii lor SMD primesc o via spre plan
PLANE_NETS = {'GND': 'In1.Cu', '+3V3': 'In2.Cu'}

def nets():
    out = {}
    for p in PARTS:
        for pin, n in p['pins'].items():
            out.setdefault(n, []).append((p['ref'], pin))
    return out

if __name__ == '__main__':
    ns = nets()
    for n in sorted(ns):
        print(f'{n:12s}', ' '.join(f'{r}.{p}' for r, p in ns[n]))
    single = [n for n, v in ns.items() if len({r for r, _ in v}) < 2]
    print(len(PARTS), 'piese,', len(ns), 'rețele; cu o singură piesă:', single)
