% Planșa: secțiune prin carcasă, în planul de mijloc al plăcii (Y = 26,9 mm).
% Cotele vin din enclosure/carcasa.scad și enclosure/placa.scad (vezi
% comentariile); cele estimate acolo sunt estimate și aici.
% Textul se pune din LaTeX, peste figură: aici doar desenul.
% Are nevoie de fiziko.mp (GPL-3.0), adus de build.sh.

prologues := 3;
outputformat := "mps";
outputtemplate := "%j.mps";
randomseed := 1907;

input fiziko.mp;

defineMinStrokeWidth(0.18pt);
defineLightDirection(-1/6pi, 1/5pi);

u := 1.35mm;                         % scara desenului: 1 mm din carcasă

% ---- cotele (mm), din carcasa.scad
pcb_w := 105.79;  ext := 2.5;        % ext = clr + perete
t_spate := 1.6;  gol_spate := 7.0;   % gol_spate = bateria (6) + 1
pcb_t := 1.6;  stiva := 11.0;  weact_t := 1.6;
panou_t := 1.2;  panou_joc := 0.2;  t_fata := 1.6;
z_pcb := t_spate + gol_spate;        % 8,6
z_pcb_f := z_pcb + pcb_t;            % 10,2
z_weact := z_pcb_f + stiva;          % 21,2
z_weact_f := z_weact + weact_t;      % 22,8
z_fata_in := z_weact_f + panou_t + panou_joc;  % 24,2
z_sus := z_fata_in + t_fata;         % 25,8
h1 := 6.395;  h2 := 98.2;            % găurile H1/H3 și H2/H4, pe X
pan_c := (h1 + h2)/2;                % centrul panoului
pan_w := 86.0;  act_w := 81.40;      % sticla și zona activă
fer_m := 0.5;  fer_tes := 1.0;       % fereastra: peste zona activă, teșitura
bat_x0 := 44 - 25;  bat_x1 := 44 + 25;  bat_t := 6;
modul_x0 := pcb_w - 25.5;  modul_h := 3.1; % ESP32-S3-WROOM-1, antena la margine
stalp_d := 5.6;  dist_d := 5.0;
varf_z := 0.8;                       % șurubul M2×25: de la 0,8 la 25,8

def P(expr px, pz) = ((px)*u, (pz)*u) enddef;
def box(expr ax, az, bx, bz) = (P(ax,az)--P(bx,az)--P(bx,bz)--P(ax,bz)--cycle) enddef;

pen fin, mediu, gros;
fin := pencircle scaled 0.22pt;
mediu := pencircle scaled 0.45pt;
gros := pencircle scaled 0.8pt;

% hașură: linii paralele la unghiul a, pasul s (pe hârtie), tăiate pe contur
vardef hasura(expr p, a, s) =
  save h, c, L; picture h; pair c;
  c := center p;  L := 80mm;
  h := image(
    for i = -L step s until L:
      draw ((-L, i)--(L, i)) rotated a shifted c withpen fin;
    endfor);
  clip h to p;
  h
enddef;

% sticla, după convenția de desen: grupe de câte trei linii scurte, oblice
vardef sticla(expr p) =
  save h, x; picture h;
  h := image(
    for x = xpart llcorner p + 6mm step 14mm until xpart lrcorner p - 2mm:
      for k = 0 upto 2:
        draw ((x + k*0.9mm, ypart llcorner p + 0.25mm)
              -- (x + k*0.9mm + 1.1mm, ypart ulcorner p - 0.25mm)) withpen fin;
      endfor;
    endfor);
  clip h to p;
  h
enddef;

beginfig(1);
  path fata, capac, pcb, weact, panou, bat, modul, p;

  % ---- rama și pereții (o singură piesă, „fata”), hașură la 45°
  fata := P(-ext, t_spate)--P(-ext, z_sus - 0.8)--P(-ext + 0.8, z_sus)
     --P(pan_c - act_w/2 - fer_m - fer_tes, z_sus)
     --P(pan_c - act_w/2 - fer_m, z_fata_in)
     --P(-0.3, z_fata_in)--P(-0.3, t_spate)--cycle;
  draw hasura(fata, 45, 0.85mm);  draw fata withpen mediu;
  p := fata reflectedabout (P(pcb_w/2, 0), P(pcb_w/2, 1));
  % rama nu e simetrică față de placă: fereastra stă pe centrul panoului
  p := P(pcb_w + ext, t_spate)--P(pcb_w + ext, z_sus - 0.8)--P(pcb_w + ext - 0.8, z_sus)
     --P(pan_c + act_w/2 + fer_m + fer_tes, z_sus)
     --P(pan_c + act_w/2 + fer_m, z_fata_in)
     --P(pcb_w + 0.3, z_fata_in)--P(pcb_w + 0.3, t_spate)--cycle;
  draw hasura(p, 45, 0.85mm);  draw p withpen mediu;

  % ---- capacul, hașură în sens invers (piesă diferită)
  capac := box(-ext, 0, pcb_w + ext, t_spate);
  draw hasura(capac, -45, 0.85mm);  draw capac withpen mediu;

  % ---- stâlpii capacului și distanțierele: în spatele planului, desenați în vedere
  for x = h1, h2:
    draw tube.l(P(x, t_spate)--P(x, z_pcb))(stalp_d/2*u);
    draw tube.l(P(x, z_pcb_f)--P(x, z_weact))(dist_d/2*u);
  endfor;

  % ---- bateria, tăiată: textura „solid” din fiziko
  bat := box(bat_x0, t_spate, bat_x1, t_spate + bat_t);
  draw solid(bat, 30, 1);  draw bat withpen mediu;

  % ---- placa noastră și placa WeAct: secțiuni subțiri, pline
  pcb := box(0, z_pcb, pcb_w, z_pcb_f);
  weact := box(0, z_weact, pcb_w, z_weact_f);
  fill pcb;  fill weact;

  % ---- modulul ESP32-S3 pe fața F
  modul := box(modul_x0, z_pcb_f, pcb_w, z_pcb_f + modul_h);
  draw hasura(modul, 0, 0.45mm);  draw modul withpen mediu;
  draw P(modul_x0 + 6.5, z_pcb_f)--P(modul_x0 + 6.5, z_pcb_f + modul_h) withpen fin;  % capătul ecranajului

  % ---- panoul (sticla) și zona activă
  panou := box(pan_c - pan_w/2, z_weact_f, pan_c + pan_w/2, z_weact_f + panou_t);
  draw sticla(panou);  draw panou withpen mediu;
  draw P(pan_c - act_w/2, z_weact_f + panou_t + 0.18)--P(pan_c + act_w/2, z_weact_f + panou_t + 0.18)
    withpen pencircle scaled 0.9pt;

  % ---- șuruburile M2×25 cu cap înecat și piulițele: sunt în spatele planului,
  % ascunse de ramă, de plăci, de stâlpi și de distanțiere, deci linii întrerupte
  picture ascuns;
  ascuns := image(
    for x = h1, h2:
      draw P(x - 1, varf_z)--P(x - 1, z_sus - 1.0)--P(x - 2, z_sus);
      draw P(x + 1, varf_z)--P(x + 1, z_sus - 1.0)--P(x + 2, z_sus);
      draw P(x - 1, varf_z)--P(x + 1, varf_z);
      draw box(x - 2, varf_z + 0.2, x + 2, varf_z + 1.8);         % piulița M2: cheia 4, înălțimea 1,6
    endfor);
  draw ascuns dashed evenly scaled 0.45 withpen pencircle scaled 0.3pt;
  for x = h1, h2:                                                % axa
    draw P(x, -2.2)--P(x, z_sus + 2.2) dashed dashpattern(on 3mm off 0.6mm on 0.4mm off 0.6mm) withpen fin;
  endfor;

  % ---- conturul exterior, gros
  draw P(-ext, 0)--P(-ext, z_sus - 0.8)--P(-ext + 0.8, z_sus) withpen gros;
  draw P(pcb_w + ext, 0)--P(pcb_w + ext, z_sus - 0.8)--P(pcb_w + ext - 0.8, z_sus) withpen gros;
  draw P(-ext, 0)--P(pcb_w + ext, 0) withpen gros;

  % ---- cotele: lățimea și grosimea
  numeric zc, xc;
  zc := -5.5;
  draw P(-ext, -0.8)--P(-ext, zc - 1.2) withpen fin;
  draw P(pcb_w + ext, -0.8)--P(pcb_w + ext, zc - 1.2) withpen fin;
  drawdblarrow P(-ext, zc)--P(pcb_w + ext, zc) withpen fin;
  xc := pcb_w + ext + 6;
  draw P(pcb_w + ext + 0.8, 0)--P(xc + 1.2, 0) withpen fin;
  draw P(pcb_w + ext - 0.4, z_sus)--P(xc + 1.2, z_sus) withpen fin;
  drawdblarrow P(xc, 0)--P(xc, z_sus) withpen fin;

  % ---- scara: 10 mm, în trepte de câte 1 mm
  for k = 0 upto 9:
    p := box(-ext + k, -10.6, -ext + k + 1, -10.0);
    if odd k: fill p; else: draw p withpen fin; fi;
  endfor;

  setbounds currentpicture to box(-ext - 2, -13, pcb_w + ext + 10, z_sus + 3);
endfig;

end.
