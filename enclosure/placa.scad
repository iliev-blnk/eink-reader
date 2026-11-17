// Generat de placa.py din hardware/pcb/eink-reader-pcb.kicad_pcb. Nu se editează de mână.
// Coordonate de carcasă: X la dreapta, Y în sus, originea în colțul stânga-jos al plăcii,
// privită din față (fața F, cu piesele, e spre ecran).

pcb_w = 105.790;
pcb_h = 53.800;

// găurile de prindere (aceleași ca pe placa WeAct): [X, Y, diametru]
gauri = [
    [6.395, 50.9, 2.2],  // H1
    [98.2, 50.9, 2.2],  // H2
    [6.395, 2.9, 2.2],  // H3
    [98.2, 2.9, 2.2],  // H4
];

// piesele: [X, Y, x0, y0, x1, y1]: centrul și dreptunghiul de curte (courtyard)
p_J1 = [40.0, 3.675, 34.635, -0.55, 45.365, 8.48];  // USB-C (GCT USB4105), marginea de jos
p_J2 = [69.0, 4.3, 66.505, 0.955, 73.495, 6.545];  // bateria (JST PH vertical)
p_J3 = [36.0, 49.8, 34.185, 47.985, 55.595, 51.615];  // bareta spre placa WeAct
p_J4 = [70.0, 47.25, 63.115, 40.655, 76.885, 53.795];  // slotul microSD (Molex 104031), marginea de sus
p_J5 = [86.0, 12.8, 84.185, 10.985, 92.895, 14.615];  // UART, baretă 1×3 verticală
p_SW1 = [28.0, 2.38, 23.555, -1.065, 32.445, 4.875];  // comutatorul pornit/oprit (PCM12), marginea de jos
p_SW2 = [2.05, 33.8, -0.295, 30.925, 4.145, 36.675];  // buton lateral ÎNAPOI, marginea din stânga
p_SW3 = [2.05, 19.8, -0.295, 16.925, 4.145, 22.675];  // buton lateral ÎNAINTE, marginea din stânga
p_SW4 = [20.0, 51.75, 17.125, 49.655, 22.875, 54.095];  // buton lateral MENIU, marginea de sus
p_U7 = [93.04, 26.9, 79.545, 5.405, 120.835, 48.395];  // ESP32-S3-WROOM-1, antena spre dreapta
