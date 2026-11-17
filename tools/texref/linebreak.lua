-- Rupe în rânduri paragrafele din fișierul TEXREF_IN cu algoritmul din LuaTeX
-- (tex.linebreak) și scrie rupturile în TEXREF_OUT. Îl pornește texref.py.
--
-- Un paragraf are două rânduri în fișierul de intrare:
--   P <id> <lățimea rândului>
--   <elementele, despărțite prin spații>
-- Lățimile sunt în 1/64 px. Un px se ia drept 1 pt, deci 1/64 px = 1024 sp.
-- Elementele (numerotate de la 0 în ordinea din fișier):
--   i W        alineatul primului rând
--   b W        o cutie: litere între două puncte de rupere
--   g W        un spațiu la care se poate rupe
--   n W        un spațiu la care nu se poate rupe (U+00A0)
--   s PRE REP  cratimă moale: PRE e lățimea cratimei dacă se rupe aici (cu
--              kerningul ei), REP kerningul dintre literele vecine dacă nu
--   d REP      rupere după o liniuță din cuvânt: REP ca mai sus
--   z          U+200B: rupere fără cost și fără lățime
--   h          U+2028: rupere forțată
--
-- Literele nu contează, doar lățimile, deci cutiile sunt linii (rule) fără
-- înălțime. Fiecare nod la care se poate rupe poartă numărul elementului într-un
-- atribut; după rupere, TeX pune la capătul rândului un \rightskip care
-- moștenește atributele nodului la care s-a rupt, și de acolo se citește ruptura.
--
-- Rezultatul, câte un rând pentru fiecare paragraf și toleranță:
--   R <id> <toleranța> <demerits> <numărul de rânduri> <elementul la care se rupe fiecare rând, fără ultimul>

local ATTR = 100
local UNIT = 1024
local RIGHTSKIP = 9
local PARFILLSKIP = 15
local TOLERANCES = {400, 10000}

local function tagged(n, item)
  node.set_attribute(n, ATTR, item + 1)
  return n
end

local function rule(w)
  local r = node.new("rule")
  r.width, r.height, r.depth = w * UNIT, 0, 0
  return r
end

local function space(w)
  local g = node.new("glue")
  g.width = w * UNIT
  g.stretch = 2 * w * UNIT
  g.shrink = (w * UNIT) // 3
  return g
end

local function penalty(v)
  local p = node.new("penalty")
  p.penalty = v
  return p
end

local function fil()
  local g = node.new("glue")
  g.stretch, g.stretch_order = 65536, 2
  return g
end

local function disc(pre, rep)
  local d = node.new("disc")
  if pre then d.pre = rule(pre) end
  if rep ~= 0 then
    local k = node.new("kern")
    k.kern = rep * UNIT
    d.replace = k
  end
  d.penalty = 100
  return d
end

local function build(tokens)
  local head, tail
  local function add(n)
    if head then tail.next = n; n.prev = tail else head = n end
    tail = n
  end
  local i, item = 1, 0
  while i <= #tokens do
    local t = tokens[i]
    if t == "b" or t == "i" then
      add(rule(tonumber(tokens[i + 1]))); i = i + 2
    elseif t == "g" then
      add(tagged(space(tonumber(tokens[i + 1])), item)); i = i + 2
    elseif t == "n" then
      add(penalty(10000)); add(space(tonumber(tokens[i + 1]))); i = i + 2
    elseif t == "s" then
      add(tagged(disc(tonumber(tokens[i + 1]), tonumber(tokens[i + 2])), item)); i = i + 3
    elseif t == "d" then
      add(tagged(disc(nil, tonumber(tokens[i + 1])), item)); i = i + 2
    elseif t == "z" then
      add(tagged(penalty(0), item)); i = i + 1
    elseif t == "h" then
      add(penalty(10000)); add(fil()); add(tagged(penalty(-10000), item)); i = i + 1
    else
      error("element necunoscut: " .. tostring(t))
    end
    item = item + 1
  end
  add(penalty(10000))
  local g = fil()
  g.subtype = PARFILLSKIP
  add(g)
  return head
end

tex.hbadness = 10000
tex.hfuzz = 1073741823

local inp = assert(io.open(os.getenv("TEXREF_IN"), "r"))
local out = assert(io.open(os.getenv("TEXREF_OUT"), "w"))
local hlist = node.id("hlist")
while true do
  local header = inp:read("l")
  if not header then break end
  local id, hsize = header:match("^P (%S+) (%d+)$")
  local tokens = {}
  for t in inp:read("l"):gmatch("%S+") do tokens[#tokens + 1] = t end
  for _, tolerance in ipairs(TOLERANCES) do
    local lines, info = tex.linebreak(build(tokens), {
      hsize = tonumber(hsize) * UNIT, pretolerance = -1, tolerance = tolerance, emergencystretch = 0,
      linepenalty = 10, hyphenpenalty = 100, exhyphenpenalty = 100,
      doublehyphendemerits = 100000, finalhyphendemerits = 5000, adjdemerits = 10000,
      looseness = 0, hangindent = 0, hangafter = 1,
    })
    local breaks = {}
    for line in node.traverse_id(hlist, lines) do
      local last = node.tail(line.head)
      local item = last and last.id == node.id("glue") and last.subtype == RIGHTSKIP and node.has_attribute(last, ATTR)
      breaks[#breaks + 1] = item and item - 1 or -1
    end
    if breaks[#breaks] ~= -1 then error("paragraful " .. id .. ": ultimul rând se termină la o rupere") end
    breaks[#breaks] = nil
    out:write(string.format("R %s %d %d %d %s\n", id, tolerance, info.demerits, info.prevgraf, table.concat(breaks, " ")))
    node.flush_list(lines)
  end
end
out:close()
