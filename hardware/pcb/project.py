"""Fișierul de proiect (.kicad_pro): clasele de rețele și regulile plăcii.

Regulile sunt ale procesului JLCPCB cu 4 straturi (trasee și spații de 0,1 mm
posibile, aici 0,15 mm, cu rezervă); `jlcpcb.kicad_dru` adaugă restul.
"""
import json, os
from circuit import POWER_NETS

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = 'eink-reader-pcb'

TRACK, CLEAR, VIA_D, VIA_DRILL = 0.15, 0.15, 0.6, 0.3
POWER_TRACK = 0.4

def netclass(name, track, prio):
    return {'name': name, 'clearance': CLEAR, 'track_width': track, 'via_diameter': VIA_D, 'via_drill': VIA_DRILL,
            'microvia_diameter': 0.3, 'microvia_drill': 0.1, 'diff_pair_width': 0.2, 'diff_pair_gap': 0.25,
            'diff_pair_via_gap': 0.25, 'wire_width': 6, 'bus_width': 12, 'line_style': 0,
            'pcb_color': 'rgba(0, 0, 0, 0.000)', 'schematic_color': 'rgba(0, 0, 0, 0.000)', 'priority': prio}

def write_pro():
    power = sorted(POWER_NETS - {'GND', '+3V3'})        # GND și +3V3 sunt planuri; fan-out-ul lor e scurt
    pro = {
        'board': {'design_settings': {
            'defaults': {'board_outline_line_width': 0.05},
            'rules': {'min_clearance': CLEAR, 'min_track_width': TRACK, 'min_via_diameter': VIA_D,
                      'min_through_hole_diameter': VIA_DRILL, 'min_via_annular_width': 0.13,
                      'min_copper_edge_clearance': 0.5, 'min_hole_to_hole': 0.5, 'min_hole_clearance': 0.25,
                      'min_silk_clearance': 0.0, 'min_text_height': 0.8, 'min_text_thickness': 0.15},
            'track_widths': [0.0, TRACK, 0.25, POWER_TRACK, 0.6],
            'via_dimensions': [{'diameter': 0.0, 'drill': 0.0}, {'diameter': VIA_D, 'drill': VIA_DRILL}],
        }},
        'meta': {'filename': f'{PROJECT}.kicad_pro', 'version': 3},
        'net_settings': {
            'classes': [netclass('Default', TRACK, 2147483647), netclass('Power', POWER_TRACK, 0)],
            'meta': {'version': 4},
            'netclass_assignments': None,
            'netclass_patterns': [{'netclass': 'Power', 'pattern': n} for n in power],
        },
    }
    json.dump(pro, open(os.path.join(HERE, f'{PROJECT}.kicad_pro'), 'w'), indent=2)
