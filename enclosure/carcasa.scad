// Carcasa cititorului (ENC-1). Parametrică; geometria plăcii vine din placa.scad,
// generat de placa.py din placa KiCad.
//
// Piese printate:
//   fata        rama cu fereastra și toți pereții; se printează cu fața pe masă
//   spate       capacul din spate, intră între pereți; cu fața exterioară pe masă
//   buton       împingătorul unui buton lateral (3 bucăți), cu flanșa pe masă
//   distantier  tub între placa noastră și placa WeAct (4 bucăți), în picioare
//
// Prinderea: 4 șuruburi M2 cu cap înecat, din față, prin găurile H1–H4:
// rama → placa WeAct → distanțier → placa noastră → stâlpul capacului → piuliță
// M2 în locașul hexagonal din fața exterioară a capacului. Strâng tot pachetul,
// deci nu trebuie cleme.
//
// Z = 0 e fața exterioară a capacului, Z crește spre ecran. X, Y ca în placa.scad
// (privit din față). Valorile marcate „estimat” sunt de măsurat pe piesele
// adevărate (lista e în README.md).

include <placa.scad>

piesa = "ansamblu"; // [ansamblu, explodat, sectiune, fata, spate, buton, distantier, print]

/* [Ecranul WeAct, estimat] */
weact_t = 1.6;            // grosimea plăcii modulului
panou_t = 1.2;            // grosimea panoului (sticlă + folie)
panou = [86.0, 50.0];     // sticla panoului (X, Y); trebuie să încapă între găuri
gauri_c = [(gauri[0][0] + gauri[1][0]) / 2, (gauri[0][1] + gauri[2][1]) / 2];
panou_c = gauri_c;        // centrul sticlei
activ = [81.40, 46.96];   // zona activă: din diagonala de 3,7" și 416×240 pixeli
activ_c = panou_c;        // centrul zonei active

/* [Placa noastră și stiva] */
pcb_t = 1.6;
stiva = 11.0;             // de la fața F a plăcii noastre la spatele plăcii WeAct:
                          // soclu mamă de 8,5 + plasticul baretei tată de 2,5 (estimat)
bat = [50, 34, 6];        // bateria LiPo: lungime (X), lățime (Y), grosime (estimat)
bat_c = [44, pcb_h / 2];  // centrul ei, departe de antenă (dreapta)

/* [Piesele de pe margini, înălțimi peste fața F, din foile de catalog] */
usb_z = 1.63;             // centrul gurii USB4105
usb_gura = [12.8, 7.0];   // gaura pentru capătul turnat al cablului
sw1_z = 0.75;             // centrul manetei PCM12
sd_z = 0.75;              // centrul cardului în Molex 104031
btn_z = 1.75;             // centrul pârghiei SKRTLAE010
// cele mai înalte piese dintre plăci: [nume, înălțime peste fața F]
inalte = [["U7 WROOM", 3.1], ["J1 USB-C", 3.26], ["J5 UART 1×3", 8.5],
          ["J2 + fișa bateriei (estimat)", 9.5]];

/* [Construcție] */
clr = 0.3;                // joc între plăci și pereți
perete = 2.2;
t_fata = 1.6;             // grosimea ramei în fața panoului
t_spate = 1.6;
raza = 3.0;               // colțurile exterioare
tesitura = 0.8;           // muchia din față
panou_joc = 0.2;          // aer între panou și ramă (sau bandă de spumă)
gol_spate = bat[2] + 1.0; // între capac și fața B a plăcii, pentru baterie
fereastra_m = 0.5;        // fereastra trece cu atât peste zona activă
fereastra_tes = 1.0;      // fereastra se lărgește spre exterior cu atât, pe fiecare latură
stalp_d = 5.6;            // stâlpii capacului
dist_d = 5.0;             // distanțierele
m2 = 2.4;                 // gaura de trecere M2
piulita = [4.0, 1.6];     // M2: deschiderea cheii, înălțimea
surub_std = [16, 20, 25, 30];
buton = [3.0, 5.0, 0.8, 1.0];  // împingător: tija, flanșa, grosimea flanșei, cât iese afară
buton_lacas = 1.0;        // adâncimea locașului flanșei în perete
sw1_fanta = 4.0;          // lungimea fantei comutatorului (maneta + cursa)
ghid = [1.2, 6, 3];       // colțarele bateriei: grosime, braț, înălțime
gravura = 0.4;

$fn = 48;
e = 0.01;

// ---------------------------------------------------------------- cotele în Z
z_pcb = t_spate + gol_spate;          // fața B a plăcii noastre
z_pcb_f = z_pcb + pcb_t;              // fața F (cu piesele)
z_weact = z_pcb_f + stiva;            // spatele plăcii WeAct
z_weact_f = z_weact + weact_t;        // fața WeAct, sub panou
z_fata_in = z_weact_f + panou_t + panou_joc;
z_sus = z_fata_in + t_fata;
ext = clr + perete;                   // de la marginea plăcii la exteriorul carcasei

// șurubul: cel mai lung standard care nu iese prin capac; piulița sub vârf
surub_l = max([for (l = surub_std) if (z_sus - l >= 0.3) l]);
varf_z = z_sus - surub_l;
piulita_z = varf_z + piulita[1] + 0.2; // fundul locașului piuliței, măsurat de la Z = 0

// ---------------------------------------------------------------- verificări
function dist_drept(p, c, s) =       // distanța de la p la dreptunghiul (centru c, mărime s)
    norm([max(abs(p[0] - c[0]) - s[0] / 2, 0), max(abs(p[1] - c[1]) - s[1] / 2, 0)]);

echo(str("carcasa: ", pcb_w + 2 * ext, " × ", pcb_h + 2 * ext, " × ", z_sus, " mm"));
echo(str("șuruburi: 4 × M2×", surub_l, " cap înecat (ISO 7046) + 4 piulițe M2; vârful la ",
         varf_z, " mm sub fața capacului"));
echo(str("distanțiere: 4 × ", stiva, " mm (cât bareta dintre plăci)"));
assert(varf_z >= 0.3, "nu există șurub standard potrivit");
assert(piulita_z <= z_pcb - 1.0, "locașul piuliței trece prin stâlp; gol_spate prea mic");
for (g = gauri) {
    d = dist_drept([g[0], g[1]], panou_c, panou + [2 * clr, 2 * clr]);
    if (d < 2.2) echo(str("ATENȚIE: gaura de la ", g[0], ", ", g[1], " e la ", d,
                          " mm de sticla panoului; piciorul ramei n-are pe ce să apese"));
}
if (activ[0] + 2 * fereastra_m > panou[0] || activ[1] + 2 * fereastra_m > panou[1])
    echo("ATENȚIE: fereastra iese din sticla panoului");
for (p = inalte) if (p[1] + 0.5 > stiva)
    echo(str("ATENȚIE: ", p[0], " (", p[1], " mm) nu încape între plăci (", stiva, " mm)"));
if (bat_c[0] + bat[0] / 2 > p_U7[2])
    echo("ATENȚIE: bateria ajunge sub modulul WiFi; antena e la dreapta");
for (g = gauri) {
    d = dist_drept([g[0], g[1]], bat_c, [bat[0], bat[1]] + 2 * [0.5 + ghid[0], 0.5 + ghid[0]]);
    if (d < stalp_d / 2) echo(str("ATENȚIE: bateria se lovește de stâlpul de la ", g[0], ", ", g[1]));
}

// ---------------------------------------------------------------- unelte
module dr(s, r) {                     // dreptunghi cu colțul în origine, colțuri rotunjite
    if (r > 0) translate([r, r]) offset(r = r) square([s[0] - 2 * r, s[1] - 2 * r]);
    else square(s);
}

// Pe un perete: u de-a lungul lui, v = Z, w = adâncimea de la fața interioară spre afară.
module pe(lat) {
    if (lat == "jos") multmatrix([[1, 0, 0, 0], [0, 0, -1, -clr], [0, 1, 0, 0]]) children();
    if (lat == "sus") multmatrix([[1, 0, 0, 0], [0, 0, 1, pcb_h + clr], [0, 1, 0, 0]]) children();
    if (lat == "stanga") multmatrix([[0, 0, -1, -clr], [1, 0, 0, 0], [0, 1, 0, 0]]) children();
    if (lat == "dreapta") multmatrix([[0, 0, 1, pcb_w + clr], [1, 0, 0, 0], [0, 1, 0, 0]]) children();
}
module cutie(u0, u1, v0, v1, w0, w1, r = 0)
    translate([u0, v0, w0]) linear_extrude(w1 - w0) dr([u1 - u0, v1 - v0], min(r, (u1 - u0) / 2 - e, (v1 - v0) / 2 - e));
module cil(u, v, w0, w1, d) translate([u, v, w0]) cylinder(h = w1 - w0, d = d);

// cât intră o piesă în perete (după dreptunghiul de curte), pe latura ei
function intra(p, lat) = max(0, (lat == "jos" ? -p[3] : lat == "sus" ? p[5] - pcb_h :
                                 lat == "stanga" ? -p[2] : p[4] - pcb_w) - clr);

// ---------------------------------------------------------------- rama (fata)
module contur_ext(i = 0) translate([-ext + i, -ext + i]) dr([pcb_w + 2 * (ext - i), pcb_h + 2 * (ext - i)], raza - i);

module fata() {
    difference() {
        union() {
            difference() {
                hull() {
                    linear_extrude(z_sus - tesitura) contur_ext();
                    translate([0, 0, z_sus - e]) linear_extrude(e) contur_ext(tesitura);
                }
                translate([-clr, -clr, -1]) cube([pcb_w + 2 * clr, pcb_h + 2 * clr, z_fata_in + 1]);
            }
            // picioarele care apasă placa WeAct în colțuri, pe lângă sticlă
            for (g = gauri) difference() {
                intersection() {
                    translate([g[0] - 3.5, g[1] - 3.5, z_weact_f]) cube([7, 7, z_fata_in - z_weact_f + e]);
                    translate([-clr, -clr, 0]) cube([pcb_w + 2 * clr, pcb_h + 2 * clr, z_sus]);
                }
                translate([panou_c[0] - panou[0] / 2 - clr, panou_c[1] - panou[1] / 2 - clr, 0])
                    cube([panou[0] + 2 * clr, panou[1] + 2 * clr, z_sus]);
            }
        }
        // fereastra, teșită spre exterior
        hull() {
            f = activ + 2 * [fereastra_m, fereastra_m];
            translate([activ_c[0] - f[0] / 2, activ_c[1] - f[1] / 2, z_fata_in - e]) cube([f[0], f[1], e]);
            g = f + 2 * [fereastra_tes, fereastra_tes];
            translate([activ_c[0] - g[0] / 2, activ_c[1] - g[1] / 2, z_sus + e]) cube([g[0], g[1], e]);
        }
        // șuruburile, cu locaș pentru capul înecat
        for (g = gauri) translate([g[0], g[1], 0]) {
            translate([0, 0, z_weact_f - 1]) cylinder(h = z_sus, d = m2);
            translate([0, 0, z_sus - 1.0]) cylinder(h = 1.0 + e, d1 = m2, d2 = m2 + 2.0);
        }
        // USB-C: gaura pentru cablu și, dedesubt, canalul pe unde trece mufa la montaj
        let(p = p_J1, z = z_pcb_f + usb_z) pe("jos") {
            cutie(p[0] - usb_gura[0] / 2, p[0] + usb_gura[0] / 2, z - usb_gura[1] / 2, z + usb_gura[1] / 2, -1, perete + 1, 1.5);
            cutie(p[2], p[4], -1, z, -1, intra(p, "jos") + 0.3);
        }
        // comutatorul: fanta, o adâncitură afară ca maneta să fie la nivelul peretelui,
        // și canalul de montaj
        let(p = p_SW1, z0 = z_pcb_f - 0.2, z1 = z_pcb_f + 2 * sw1_z + 0.5) pe("jos") {
            cutie(p[0] - sw1_fanta / 2, p[0] + sw1_fanta / 2, z0, z1, -1, perete + 1, 0.5);
            cutie(p[0] - 5, p[0] + 5, z0, z1 + 2.5, perete - (intra(p, "jos") + 0.05), perete + 1, 1.5);
            cutie(p[0] - sw1_fanta / 2, p[0] + sw1_fanta / 2, -1, z0 + e, -1, intra(p, "jos") + 0.3);
        }
        // microSD: fanta și o adâncitură pentru deget
        let(p = p_J4, z = z_pcb_f + sd_z) pe("sus") {
            cutie(p[0] - 6.25, p[0] + 6.25, z - 1.0, z + 1.0, -1, perete + 1, 0.6);
            cutie(p[0] - 8, p[0] + 8, z - 4, z + 4, 0.8, perete + 1, 3);
        }
        // butoanele: gaura tijei și locașul flanșei
        for (b = [["stanga", p_SW2[1]], ["stanga", p_SW3[1]], ["sus", p_SW4[0]]]) pe(b[0]) {
            cil(b[1], z_pcb_f + btn_z, -1, perete + 1, buton[0] + 0.4);
            cil(b[1], z_pcb_f + btn_z, -1, buton_lacas, buton[1] + 0.6);
        }
        // firul bateriei: din spatele plăcii, pe lângă marginea de jos, la J2
        pe("jos") cutie(p_J2[0] - 2.5, p_J2[0] + 2.5, -1, z_weact - 0.5, -1, 1.5);
    }
}

// ---------------------------------------------------------------- capacul (spate)
module hex(h) cylinder(h = h, d = (piulita[0] + 0.3) * 2 / sqrt(3), $fn = 6);

module spate() {
    j = 0.15;                          // jocul capacului între pereți
    difference() {
        union() {
            translate([-clr + j, -clr + j, 0]) cube([pcb_w + 2 * (clr - j), pcb_h + 2 * (clr - j), t_spate]);
            for (g = gauri) translate([g[0], g[1], 0]) cylinder(h = z_pcb, d = stalp_d);
            // colțarele bateriei
            s = [bat[0] + 1, bat[1] + 1];
            for (i = [0, 1], k = [0, 1])
                translate([bat_c[0] + (i - 0.5) * s[0], bat_c[1] + (k - 0.5) * s[1], t_spate - e])
                    scale([1 - 2 * i, 1 - 2 * k, 1]) translate([-ghid[0], -ghid[0], 0]) {
                        cube([ghid[1], ghid[0], ghid[2]]);
                        cube([ghid[0], ghid[1], ghid[2]]);
                    }
        }
        for (g = gauri) translate([g[0], g[1], -1]) {
            cylinder(h = z_pcb + 2, d = m2);
            hex(piulita_z + 1);
        }
        // gravura, în oglindă: se citește privind capacul din spate
        translate([pcb_w / 2, pcb_h / 2, -e]) mirror([1, 0, 0]) linear_extrude(gravura + e) {
            translate([0, 7]) text("Beleniuc Ilie", size = 5, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
            translate([0, 0]) text("Proiect de licență", size = 3.2, font = "Liberation Sans", halign = "center", valign = "center");
            translate([0, -6]) text("Universitatea „Ștefan cel Mare” din Suceava", size = 2.4, font = "Liberation Sans", halign = "center", valign = "center");
        }
    }
}

// ---------------------------------------------------------------- piesele mici
module buton_piesa() {
    cylinder(h = buton[2], d = buton[1]);
    cylinder(h = buton[2] + perete - buton_lacas + buton[3], d = buton[0]);
}
module distantier() difference() {
    cylinder(h = stiva, d = dist_d);
    translate([0, 0, -1]) cylinder(h = stiva + 2, d = m2);
}

// ---------------------------------------------------------------- machetă
module placa_noastra() color("#2e6b3a") difference() {
    translate([0, 0, z_pcb]) cube([pcb_w, pcb_h, pcb_t]);
    for (g = gauri) translate([g[0], g[1], z_pcb - 1]) cylinder(h = pcb_t + 2, d = g[2]);
}
module piese() {
    for (p = [[p_U7, 3.1, "#888"], [p_J1, 3.26, "#bbb"], [p_J4, 1.4, "#bbb"], [p_J5, 8.5, "#222"],
              [p_J2, 6.0, "#eee"], [p_SW1, 1.5, "#444"], [p_SW2, 3.5, "#ddd"], [p_SW3, 3.5, "#ddd"],
              [p_SW4, 3.5, "#ddd"], [p_J3, 2.5, "#222"]])
        // dreptunghiul de curte, fără marginea lui de 0,25 și fără zona liberă de lângă antenă
        let(q = p[0], m = 0.25, x1 = min(q[4] - m, pcb_w)) color(p[2])
            translate([q[2] + m, q[3] + m, z_pcb_f]) cube([x1 - q[2] - m, q[5] - q[3] - 2 * m, p[1]]);
}
module weact() {
    color("#1d3557") difference() {
        translate([0, 0, z_weact]) cube([pcb_w, pcb_h, weact_t]);
        for (g = gauri) translate([g[0], g[1], z_weact - 1]) cylinder(h = weact_t + 2, d = g[2]);
    }
    color("#d9d9d4") translate([panou_c[0] - panou[0] / 2, panou_c[1] - panou[1] / 2, z_weact_f]) cube([panou[0], panou[1], panou_t]);
    color("#f2f2ec") translate([activ_c[0] - activ[0] / 2, activ_c[1] - activ[1] / 2, z_weact_f + panou_t - 0.05]) cube([activ[0], activ[1], 0.1]);
}
module baterie() color("#c9a227") translate([bat_c[0] - bat[0] / 2, bat_c[1] - bat[1] / 2, z_pcb - bat[2] - 0.5]) cube(bat);
module suruburi() color("#999") for (g = gauri) translate([g[0], g[1], varf_z]) cylinder(h = surub_l, d = 2);
module interior() {
    placa_noastra(); piese(); weact(); baterie(); suruburi();
    color("#e9c46a") for (g = gauri) translate([g[0], g[1], z_pcb_f]) distantier();
    // împingătoarele, cu flanșa în locașul din perete, lipită de pârghie
    a = clr + buton_lacas - buton[2];
    color("#e76f51") for (b = [[-a, p_SW2[1], 0], [-a, p_SW3[1], 0], [p_SW4[0], pcb_h + a, 1]])
        translate([b[0], b[1], z_pcb_f + btn_z]) rotate(b[2] ? [-90, 0, 0] : [0, -90, 0]) buton_piesa();
}

if (piesa == "fata") translate([0, 0, z_sus]) mirror([0, 0, 1]) fata();
if (piesa == "spate") spate();
if (piesa == "buton") buton_piesa();
if (piesa == "distantier") distantier();
if (piesa == "print") {             // tot ce se printează, așezat pe masă
    translate([0, 0, z_sus]) mirror([0, 0, 1]) fata();
    translate([0, -pcb_h - 2 * ext - 6, 0]) spate();
    for (i = [0 : 2]) translate([pcb_w + ext + 8, i * 8, 0]) buton_piesa();
    for (i = [0 : 3]) translate([pcb_w + ext + 16, i * 8, 0]) distantier();
}
if (piesa == "ansamblu") { color("#3a3d42") fata(); color("#3a3d42") spate(); interior(); }
if (piesa == "explodat") {
    color("#3a3d42") translate([0, 0, 30]) fata();
    color("#3a3d42") translate([0, 0, -16]) spate();
    interior();
}
// Secțiunea în planul Y = sectiune_y, privită din față-jos: X la dreapta, Z în sus.
sectiune_y = 2.9;                   // prin H3/H4 (șuruburi, stâlpi, distanțiere) și USB-C
module taiat() mirror([0, 1]) projection(cut = true) rotate([90, 0, 0]) translate([0, -sectiune_y, 0]) children();
if (piesa == "sectiune") {
    color("#3a3d42") taiat() fata();
    color("#5a5e66") taiat() spate();
    color("#2e6b3a") taiat() placa_noastra();
    color("#1d3557") taiat() weact();
    if (abs(sectiune_y - bat_c[1]) < bat[1] / 2) color("#c9a227") taiat() baterie();
    color("#e9c46a") taiat() for (g = gauri) translate([g[0], g[1], z_pcb_f]) distantier();
    color("#999") taiat() suruburi();
    color("#bbb") taiat() piese();
}
