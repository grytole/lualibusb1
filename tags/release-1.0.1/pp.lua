#!/usr/bin/env lua
local loadstring = loadstring or load

local function parseDollarParen(pieces, chunk, s, e)
  local append, format = table.insert, string.format
  local s = 1
  for term, executed, e in chunk:gmatch("()$$(%b())()") do
      append(pieces,
        format("%q..(%s or '')..", chunk:sub(s, term - 1), executed))
      s = e
  end
  append(pieces, format("%q", chunk:sub(s)))
end
-------------------------------------------------------------------------------
local function parseHashLines(chunk)
  local append = table.insert
  local pieces, s = {"return function(_put) ", n = 1}, 1
  while true do
    local ss, e, lua = chunk:find("^#%+([^\n]*\n?)", s)
    if not e then
      ss, e, lua = chunk:find("\n#%+([^\n]*\n?)", s)
      append(pieces, "_put(")
      parseDollarParen(pieces, chunk:sub(s, ss))
      append(pieces, ")")
      if not e then break end
    end
    append(pieces, lua)
    s = e + 1
  end
  append(pieces, " end")
  return table.concat(pieces)
end
-------------------------------------------------------------------------------
local function preprocess(chunk, name)
  local code = parseHashLines(chunk)
  return assert(loadstring(code, name and ('@'..name) or "=stdin"))()
end
------------------------------------------------------------------------------
-- Variable lookup order: globals, parameters, environment
setmetatable(_G, {__index = function(t, k) return os.getenv(k) end})

-- preprocess from stdin to stdout
if arg[1] then io.input(arg[1]) end
if arg[2] then io.output(arg[2]) end
preprocess(io.read"*a", arg[1])(io.write)
