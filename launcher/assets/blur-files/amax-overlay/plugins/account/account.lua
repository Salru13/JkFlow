-- JkFlow account plugin for the AMAX client (amax/plugins/account/account.lua).
-- At Play the JkFlow launcher writes amax/plugins/account_cfg.lua with the
-- player's AMAX username and password (Settings > Online account), or
-- deletes it when no account is set; this plugin deletes it once read.
-- Before and after each screen change this plugin copies them into Blur's profile with Amax.UpdateAccountDetails,
-- so the online login form is pre-filled. Blur's profile load from blursave
-- can overwrite them after our write (owner finding 14, 2026-11-15: the form
-- showed the account one start late), so the plugin writes again whenever
-- they are missing and stops only once they have survived a whole screen
-- change; after that an edit made in the game is left alone.
-- Blur copies each value into a 32-byte field without a bound: anything
-- longer than 31 bytes is ignored here. Nothing is printed or logged.
local M = { info = { name = "BlurAccount", version = 1.1 } }
local held = 0        -- hooks in a row that found the values in place
local done = false
local forgotten = false

-- The launcher's pre-fill file holds the password in plain text (owner
-- finding 27): once read, the values stay loaded (package.loaded) and the
-- file is deleted, found the way require found it. Where this Lua has no
-- package.searchpath or os.remove, the launcher deletes it at its next start.
local function forget_file()
  if forgotten then return end
  forgotten = true
  if type(package) ~= "table" or type(package.searchpath) ~= "function" then return end
  if type(os) ~= "table" or type(os.remove) ~= "function" then return end
  pcall(function()
    local path = package.searchpath("account_cfg", package.path)
    if type(path) == "string" then os.remove(path) end
  end)
end

local function apply()
  if done then return end
  local okc, cfg = pcall(require, "account_cfg")
  if not okc or type(cfg) ~= "table" then return end
  forget_file()
  local u, p = cfg.username, cfg.password
  if type(u) ~= "string" or type(p) ~= "string" then return end
  if #u == 0 or #p == 0 or #u > 31 or #p > 31 then return end
  local okg, cur = pcall(Amax.UpdateAccountDetails)
  if not okg or type(cur) ~= "table" then return end   -- no profile yet: try again on the next screen
  if cur.Username == u and cur.Password == p then
    held = held + 1
    -- Three hooks in a row (before and after one screen change, and the
    -- next one): they survived a screen change, stop.
    if held >= 3 then done = true end
    return
  end
  held = 0                                             -- missing or overwritten: write again
  pcall(Amax.UpdateAccountDetails, { Username = u, Password = p })
end

function M.GoScreen(screen) apply() end
function M.PushScreen(screen) apply() end
function M.Post_GoScreen(screen) apply() end
function M.Post_PushScreen(screen) apply() end
function M.init() return true end

return M
