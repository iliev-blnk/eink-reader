% Planșa: principiul panoului electroforetic, în secțiune schematică.
% Un strat de microcapsule între un electrod comun, transparent, și electrozii
% pixelilor. În fiecare capsulă, particule albe și negre cu sarcini opuse;
% tensiunea pixelului le aduce pe unele sau pe altele sub sticlă.
% Proporțiile nu sunt la scară. Textul se pune din LaTeX.
% Are nevoie de fiziko.mp (GPL-3.0), adus de build.sh.

prologues := 3;
outputformat := "mps";
outputtemplate := "%j.mps";
randomseed := 1998;

input fiziko.mp;

defineMinStrokeWidth(0.16pt);
defineLightDirection(-1/5pi, 1/4pi);

pen fin, mediu, gros;
fin := pencircle scaled 0.22pt;
mediu := pencircle scaled 0.45pt;
gros := pencircle scaled 0.8pt;

R := 9.2mm;            % raza capsulei
Ry := 7.6mm;           % capsulele stau turtite între electrozi
pas := 19.6mm;         % distanța dintre centre
n := 7;                % capsule
y_jos := -Ry - 1.2mm;  % fața electrozilor pixelilor
y_sus := Ry + 1.2mm;   % fața electrodului comun
dp := 1.55mm;          % diametrul unei particule

% starea pe orizontală: pixelul de sub x arată alb (1) sau negru (0)
% trei pixeli: alb | negru | alb; capsula a cincea stă pe granița a doi pixeli
xa := -(n - 1)/2*pas;
g1 := xa + 1.5pas;     % granița pixel 1 / pixel 2
g2 := xa + 4pas;       % granița pixel 2 / pixel 3, prin mijlocul capsulei a cincea
xmin := xa - pas/2 - 1mm;  xmax := -xmin;
vardef alb_sus(expr x) = (x < g1) or (x > g2) enddef;

vardef hasura(expr p, a, s) =
  save h, c, L; picture h; pair c;
  c := center p;  L := 120mm;
  h := image(
    for i = -L step s until L:
      draw ((-L, i)--(L, i)) rotated a shifted c withpen fin;
    endfor);
  clip h to p;
  h
enddef;

def neagra(expr c) =
  fill fullcircle scaled dp shifted c;
  fill fullcircle scaled (0.32dp) shifted (c + (-0.2dp, 0.2dp)) withcolor white;
enddef;

beginfig(1);
  path sus, jos, p;
  picture alba;
  alba := sphere(dp);

  % ---- electrodul comun și folia de deasupra (privitorul e sus)
  sus := (xmin, y_sus)--(xmax, y_sus)--(xmax, y_sus + 3.2mm)--(xmin, y_sus + 3.2mm)--cycle;
  for x = xmin + 8mm step 16mm until xmax - 4mm:
    for k = 0 upto 2:
      draw ((x + k*0.9mm, y_sus + 0.9mm)--(x + k*0.9mm + 1.6mm, y_sus + 2.9mm)) withpen fin;
    endfor;
  endfor;
  draw sus withpen mediu;
  draw (xmin, y_sus + 0.45mm)--(xmax, y_sus + 0.45mm) withpen fin;   % stratul conductor (ITO)

  % ---- substratul și electrozii pixelilor, despărțiți de goluri mici
  jos := (xmin, y_jos - 3.2mm)--(xmax, y_jos - 3.2mm)--(xmax, y_jos - 1.0mm)--(xmin, y_jos - 1.0mm)--cycle;
  draw hasura(jos, 45, 0.7mm);  draw jos withpen mediu;
  for seg = (xmin, g1 - 0.6mm), (g1 + 0.6mm, g2 - 0.6mm), (g2 + 0.6mm, xmax):
    p := (xpart seg, y_jos - 1.0mm)--(ypart seg, y_jos - 1.0mm)--(ypart seg, y_jos)--(xpart seg, y_jos)--cycle;
    fill p;
  endfor;

  % ---- liantul dintre capsule: puncte rare
  for k = 0 upto 900:
    pair q;  q := (xmin + uniformdeviate(xmax - xmin), y_jos + uniformdeviate(y_sus - y_jos));
    boolean liber;  liber := true;
    for i = 0 upto n - 1:
      if abs((xpart q - (xa + i*pas))/(R + 0.5mm), ypart q/(Ry + 0.5mm)) < 1: liber := false; fi;
    endfor;
    if liber: draw q withpen pencircle scaled 0.35pt; fi;
  endfor;

  % ---- capsulele
  for i = 0 upto n - 1:
    numeric cx;  cx := xa + i*pas;
    path m;  m := fullcircle xscaled 2R yscaled 2Ry shifted (cx, 0);
    unfill m;
    % particulele: puse la întâmplare, fără suprapuneri; cele aduse sus stau
    % strânse sub sticlă, celelalte jos, iar câteva rătăcesc prin mijloc
    pair pt[];  numeric np;  np := 0;
    for k = 0 upto 3000:
      pair q;  q := (cx - R + uniformdeviate(2R), -Ry + uniformdeviate(2Ry));
      if abs((xpart q - cx)/(R - 0.6dp), ypart q/(Ry - 0.6dp)) < 1:
        boolean ok;  ok := true;
        for j = 1 upto np: if abs(q - pt[j]) < 1.08dp: ok := false; fi; endfor;
        if ok and (uniformdeviate(1) < (abs(ypart q)/Ry)**2): np := np + 1;  pt[np] := q; fi;
      fi;
    endfor;
    for j = 1 upto np:
      boolean sus_alb;  sus_alb := alb_sus(xpart pt[j]);
      boolean e_sus;  e_sus := ypart pt[j] > 0;
      if sus_alb = e_sus: draw alba shifted pt[j]; else: neagra(pt[j]); fi;
    endfor;
    draw m withpen mediu;
    draw fullcircle xscaled (2R - 0.7mm) yscaled (2Ry - 0.7mm) shifted (cx, 0) withpen fin;
  endfor;

  % ---- granițele pixelilor, prelungite punctat prin strat
  for g = g1, g2:
    draw (g, y_jos - 4.5mm)--(g, y_sus + 5.5mm) dashed withdots scaled 0.35 withpen pencircle scaled 0.5pt;
  endfor;

  setbounds currentpicture to (xmin - 2mm, y_jos - 7mm)--(xmax + 2mm, y_jos - 7mm)
    --(xmax + 2mm, y_sus + 6mm)--(xmin - 2mm, y_sus + 6mm)--cycle;
endfig;

end.
