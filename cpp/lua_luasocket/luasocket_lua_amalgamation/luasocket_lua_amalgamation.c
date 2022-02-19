#include "luasocket_lua_amalgamation.h"
#include "lauxlib.h"
#include "lualib.h"

const char *luasocket_ftp_luafilesource = "-----------------------------------------------------------------------------\n\
-- FTP support for the Lua language\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module and import dependencies\n\
-----------------------------------------------------------------------------\n\
local base = _G\n\
local table = require(\"table\")\n\
local string = require(\"string\")\n\
local math = require(\"math\")\n\
local socket = require(\"socket\")\n\
local url = require(\"socket.url\")\n\
local tp = require(\"socket.tp\")\n\
local ltn12 = require(\"ltn12\")\n\
socket.ftp = {}\n\
local _M = socket.ftp\n\
-----------------------------------------------------------------------------\n\
-- Program constants\n\
-----------------------------------------------------------------------------\n\
-- timeout in seconds before the program gives up on a connection\n\
_M.TIMEOUT = 60\n\
-- default port for ftp service\n\
local PORT = 21\n\
-- this is the default anonymous password. used when no password is\n\
-- provided in url. should be changed to your e-mail.\n\
_M.USER = \"ftp\"\n\
_M.PASSWORD = \"anonymous@anonymous.org\"\n\
\n\
-----------------------------------------------------------------------------\n\
-- Low level FTP API\n\
-----------------------------------------------------------------------------\n\
local metat = { __index = {} }\n\
\n\
function _M.open(server, port, create)\n\
    local tp = socket.try(tp.connect(server, port or PORT, _M.TIMEOUT, create))\n\
    local f = base.setmetatable({ tp = tp }, metat)\n\
    -- make sure everything gets closed in an exception\n\
    f.try = socket.newtry(function() f:close() end)\n\
    return f\n\
end\n\
\n\
function metat.__index:portconnect()\n\
    self.try(self.server:settimeout(_M.TIMEOUT))\n\
    self.data = self.try(self.server:accept())\n\
    self.try(self.data:settimeout(_M.TIMEOUT))\n\
end\n\
\n\
function metat.__index:pasvconnect()\n\
    self.data = self.try(socket.tcp())\n\
    self.try(self.data:settimeout(_M.TIMEOUT))\n\
    self.try(self.data:connect(self.pasvt.address, self.pasvt.port))\n\
end\n\
\n\
function metat.__index:login(user, password)\n\
    self.try(self.tp:command(\"user\", user or _M.USER))\n\
    local code, reply = self.try(self.tp:check{\"2..\", 331})\n\
    if code == 331 then\n\
        self.try(self.tp:command(\"pass\", password or _M.PASSWORD))\n\
        self.try(self.tp:check(\"2..\"))\n\
    end\n\
    return 1\n\
end\n\
\n\
function metat.__index:pasv()\n\
    self.try(self.tp:command(\"pasv\"))\n\
    local code, reply = self.try(self.tp:check(\"2..\"))\n\
    local pattern = \"(%d+)%D(%d+)%D(%d+)%D(%d+)%D(%d+)%D(%d+)\"\n\
    local a, b, c, d, p1, p2 = socket.skip(2, string.find(reply, pattern))\n\
    self.try(a and b and c and d and p1 and p2, reply)\n\
    self.pasvt = {\n\
        address = string.format(\"%d.%d.%d.%d\", a, b, c, d),\n\
        port = p1*256 + p2\n\
    }\n\
    if self.server then\n\
        self.server:close()\n\
        self.server = nil\n\
    end\n\
    return self.pasvt.address, self.pasvt.port\n\
end\n\
\n\
function metat.__index:epsv()\n\
    self.try(self.tp:command(\"epsv\"))\n\
    local code, reply = self.try(self.tp:check(\"229\"))\n\
    local pattern = \"%((.)(.-)%1(.-)%1(.-)%1%)\"\n\
    local d, prt, address, port = string.match(reply, pattern)\n\
    self.try(port, \"invalid epsv response\")\n\
    self.pasvt = {\n\
        address = self.tp:getpeername(),\n\
        port = port\n\
    }\n\
    if self.server then\n\
        self.server:close()\n\
        self.server = nil\n\
    end\n\
    return self.pasvt.address, self.pasvt.port\n\
end\n\
\n\
\n\
function metat.__index:port(address, port)\n\
    self.pasvt = nil\n\
    if not address then\n\
        address, port = self.try(self.tp:getsockname())\n\
        self.server = self.try(socket.bind(address, 0))\n\
        address, port = self.try(self.server:getsockname())\n\
        self.try(self.server:settimeout(_M.TIMEOUT))\n\
    end\n\
    local pl = math.mod(port, 256)\n\
    local ph = (port - pl)/256\n\
    local arg = string.gsub(string.format(\"%s,%d,%d\", address, ph, pl), \"%.\", \",\")\n\
    self.try(self.tp:command(\"port\", arg))\n\
    self.try(self.tp:check(\"2..\"))\n\
    return 1\n\
end\n\
\n\
function metat.__index:eprt(family, address, port)\n\
    self.pasvt = nil\n\
    if not address then\n\
        address, port = self.try(self.tp:getsockname())\n\
        self.server = self.try(socket.bind(address, 0))\n\
        address, port = self.try(self.server:getsockname())\n\
        self.try(self.server:settimeout(_M.TIMEOUT))\n\
    end\n\
    local arg = string.format(\"|%s|%s|%d|\", family, address, port)\n\
    self.try(self.tp:command(\"eprt\", arg))\n\
    self.try(self.tp:check(\"2..\"))\n\
    return 1\n\
end\n\
\n\
\n\
function metat.__index:send(sendt)\n\
    self.try(self.pasvt or self.server, \"need port or pasv first\")\n\
    -- if there is a pasvt table, we already sent a PASV command\n\
    -- we just get the data connection into self.data\n\
    if self.pasvt then self:pasvconnect() end\n\
    -- get the transfer argument and command\n\
    local argument = sendt.argument or\n\
        url.unescape(string.gsub(sendt.path or \"\", \"^[/\\\\]\", \"\"))\n\
    if argument == \"\" then argument = nil end\n\
    local command = sendt.command or \"stor\"\n\
    -- send the transfer command and check the reply\n\
    self.try(self.tp:command(command, argument))\n\
    local code, reply = self.try(self.tp:check{\"2..\", \"1..\"})\n\
    -- if there is not a pasvt table, then there is a server\n\
    -- and we already sent a PORT command\n\
    if not self.pasvt then self:portconnect() end\n\
    -- get the sink, source and step for the transfer\n\
    local step = sendt.step or ltn12.pump.step\n\
    local readt = { self.tp }\n\
    local checkstep = function(src, snk)\n\
        -- check status in control connection while downloading\n\
        local readyt = socket.select(readt, nil, 0)\n\
        if readyt[tp] then code = self.try(self.tp:check(\"2..\")) end\n\
        return step(src, snk)\n\
    end\n\
    local sink = socket.sink(\"close-when-done\", self.data)\n\
    -- transfer all data and check error\n\
    self.try(ltn12.pump.all(sendt.source, sink, checkstep))\n\
    if string.find(code, \"1..\") then self.try(self.tp:check(\"2..\")) end\n\
    -- done with data connection\n\
    self.data:close()\n\
    -- find out how many bytes were sent\n\
    local sent = socket.skip(1, self.data:getstats())\n\
    self.data = nil\n\
    return sent\n\
end\n\
\n\
function metat.__index:receive(recvt)\n\
    self.try(self.pasvt or self.server, \"need port or pasv first\")\n\
    if self.pasvt then self:pasvconnect() end\n\
    local argument = recvt.argument or\n\
        url.unescape(string.gsub(recvt.path or \"\", \"^[/\\\\]\", \"\"))\n\
    if argument == \"\" then argument = nil end\n\
    local command = recvt.command or \"retr\"\n\
    self.try(self.tp:command(command, argument))\n\
    local code,reply = self.try(self.tp:check{\"1..\", \"2..\"})\n\
    if (code >= 200) and (code <= 299) then\n\
        recvt.sink(reply)\n\
        return 1\n\
    end\n\
    if not self.pasvt then self:portconnect() end\n\
    local source = socket.source(\"until-closed\", self.data)\n\
    local step = recvt.step or ltn12.pump.step\n\
    self.try(ltn12.pump.all(source, recvt.sink, step))\n\
    if string.find(code, \"1..\") then self.try(self.tp:check(\"2..\")) end\n\
    self.data:close()\n\
    self.data = nil\n\
    return 1\n\
end\n\
\n\
function metat.__index:cwd(dir)\n\
    self.try(self.tp:command(\"cwd\", dir))\n\
    self.try(self.tp:check(250))\n\
    return 1\n\
end\n\
\n\
function metat.__index:type(type)\n\
    self.try(self.tp:command(\"type\", type))\n\
    self.try(self.tp:check(200))\n\
    return 1\n\
end\n\
\n\
function metat.__index:greet()\n\
    local code = self.try(self.tp:check{\"1..\", \"2..\"})\n\
    if string.find(code, \"1..\") then self.try(self.tp:check(\"2..\")) end\n\
    return 1\n\
end\n\
\n\
function metat.__index:quit()\n\
    self.try(self.tp:command(\"quit\"))\n\
    self.try(self.tp:check(\"2..\"))\n\
    return 1\n\
end\n\
\n\
function metat.__index:close()\n\
    if self.data then self.data:close() end\n\
    if self.server then self.server:close() end\n\
    return self.tp:close()\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- High level FTP API\n\
-----------------------------------------------------------------------------\n\
local function override(t)\n\
    if t.url then\n\
        local u = url.parse(t.url)\n\
        for i,v in base.pairs(t) do\n\
            u[i] = v\n\
        end\n\
        return u\n\
    else return t end\n\
end\n\
\n\
local function tput(putt)\n\
    putt = override(putt)\n\
    socket.try(putt.host, \"missing hostname\")\n\
    local f = _M.open(putt.host, putt.port, putt.create)\n\
    f:greet()\n\
    f:login(putt.user, putt.password)\n\
    if putt.type then f:type(putt.type) end\n\
    f:epsv()\n\
    local sent = f:send(putt)\n\
    f:quit()\n\
    f:close()\n\
    return sent\n\
end\n\
\n\
local default = {\n\
    path = \"/\",\n\
    scheme = \"ftp\"\n\
}\n\
\n\
local function genericform(u)\n\
    local t = socket.try(url.parse(u, default))\n\
    socket.try(t.scheme == \"ftp\", \"wrong scheme '\" .. t.scheme .. \"'\")\n\
    socket.try(t.host, \"missing hostname\")\n\
    local pat = \"^type=(.)$\"\n\
    if t.params then\n\
        t.type = socket.skip(2, string.find(t.params, pat))\n\
        socket.try(t.type == \"a\" or t.type == \"i\",\n\
            \"invalid type '\" .. t.type .. \"'\")\n\
    end\n\
    return t\n\
end\n\
\n\
_M.genericform = genericform\n\
\n\
local function sput(u, body)\n\
    local putt = genericform(u)\n\
    putt.source = ltn12.source.string(body)\n\
    return tput(putt)\n\
end\n\
\n\
_M.put = socket.protect(function(putt, body)\n\
    if base.type(putt) == \"string\" then return sput(putt, body)\n\
    else return tput(putt) end\n\
end)\n\
\n\
local function tget(gett)\n\
    gett = override(gett)\n\
    socket.try(gett.host, \"missing hostname\")\n\
    local f = _M.open(gett.host, gett.port, gett.create)\n\
    f:greet()\n\
    f:login(gett.user, gett.password)\n\
    if gett.type then f:type(gett.type) end\n\
    f:epsv()\n\
    f:receive(gett)\n\
    f:quit()\n\
    return f:close()\n\
end\n\
\n\
local function sget(u)\n\
    local gett = genericform(u)\n\
    local t = {}\n\
    gett.sink = ltn12.sink.table(t)\n\
    tget(gett)\n\
    return table.concat(t)\n\
end\n\
\n\
_M.command = socket.protect(function(cmdt)\n\
    cmdt = override(cmdt)\n\
    socket.try(cmdt.host, \"missing hostname\")\n\
    socket.try(cmdt.command, \"missing command\")\n\
    local f = _M.open(cmdt.host, cmdt.port, cmdt.create)\n\
    f:greet()\n\
    f:login(cmdt.user, cmdt.password)\n\
    if type(cmdt.command) == \"table\" then\n\
        local argument = cmdt.argument or {}\n\
        local check = cmdt.check or {}\n\
        for i,cmd in ipairs(cmdt.command) do\n\
            f.try(f.tp:command(cmd, argument[i]))\n\
            if check[i] then f.try(f.tp:check(check[i])) end\n\
        end\n\
    else\n\
        f.try(f.tp:command(cmdt.command, cmdt.argument))\n\
        if cmdt.check then f.try(f.tp:check(cmdt.check)) end\n\
    end\n\
    f:quit()\n\
    return f:close()\n\
end)\n\
\n\
_M.get = socket.protect(function(gett)\n\
    if base.type(gett) == \"string\" then return sget(gett)\n\
    else return tget(gett) end\n\
end)\n\
\n\
return _M\n\
";
const char *luasocket_headers_luafilesource = "-----------------------------------------------------------------------------\n\
-- Canonic header field capitalization\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
local socket = require(\"socket\")\n\
socket.headers = {}\n\
local _M = socket.headers\n\
\n\
_M.canonic = {\n\
    [\"accept\"] = \"Accept\",\n\
    [\"accept-charset\"] = \"Accept-Charset\",\n\
    [\"accept-encoding\"] = \"Accept-Encoding\",\n\
    [\"accept-language\"] = \"Accept-Language\",\n\
    [\"accept-ranges\"] = \"Accept-Ranges\",\n\
    [\"action\"] = \"Action\",\n\
    [\"alternate-recipient\"] = \"Alternate-Recipient\",\n\
    [\"age\"] = \"Age\",\n\
    [\"allow\"] = \"Allow\",\n\
    [\"arrival-date\"] = \"Arrival-Date\",\n\
    [\"authorization\"] = \"Authorization\",\n\
    [\"bcc\"] = \"Bcc\",\n\
    [\"cache-control\"] = \"Cache-Control\",\n\
    [\"cc\"] = \"Cc\",\n\
    [\"comments\"] = \"Comments\",\n\
    [\"connection\"] = \"Connection\",\n\
    [\"content-description\"] = \"Content-Description\",\n\
    [\"content-disposition\"] = \"Content-Disposition\",\n\
    [\"content-encoding\"] = \"Content-Encoding\",\n\
    [\"content-id\"] = \"Content-ID\",\n\
    [\"content-language\"] = \"Content-Language\",\n\
    [\"content-length\"] = \"Content-Length\",\n\
    [\"content-location\"] = \"Content-Location\",\n\
    [\"content-md5\"] = \"Content-MD5\",\n\
    [\"content-range\"] = \"Content-Range\",\n\
    [\"content-transfer-encoding\"] = \"Content-Transfer-Encoding\",\n\
    [\"content-type\"] = \"Content-Type\",\n\
    [\"cookie\"] = \"Cookie\",\n\
    [\"date\"] = \"Date\",\n\
    [\"diagnostic-code\"] = \"Diagnostic-Code\",\n\
    [\"dsn-gateway\"] = \"DSN-Gateway\",\n\
    [\"etag\"] = \"ETag\",\n\
    [\"expect\"] = \"Expect\",\n\
    [\"expires\"] = \"Expires\",\n\
    [\"final-log-id\"] = \"Final-Log-ID\",\n\
    [\"final-recipient\"] = \"Final-Recipient\",\n\
    [\"from\"] = \"From\",\n\
    [\"host\"] = \"Host\",\n\
    [\"if-match\"] = \"If-Match\",\n\
    [\"if-modified-since\"] = \"If-Modified-Since\",\n\
    [\"if-none-match\"] = \"If-None-Match\",\n\
    [\"if-range\"] = \"If-Range\",\n\
    [\"if-unmodified-since\"] = \"If-Unmodified-Since\",\n\
    [\"in-reply-to\"] = \"In-Reply-To\",\n\
    [\"keywords\"] = \"Keywords\",\n\
    [\"last-attempt-date\"] = \"Last-Attempt-Date\",\n\
    [\"last-modified\"] = \"Last-Modified\",\n\
    [\"location\"] = \"Location\",\n\
    [\"max-forwards\"] = \"Max-Forwards\",\n\
    [\"message-id\"] = \"Message-ID\",\n\
    [\"mime-version\"] = \"MIME-Version\",\n\
    [\"original-envelope-id\"] = \"Original-Envelope-ID\",\n\
    [\"original-recipient\"] = \"Original-Recipient\",\n\
    [\"pragma\"] = \"Pragma\",\n\
    [\"proxy-authenticate\"] = \"Proxy-Authenticate\",\n\
    [\"proxy-authorization\"] = \"Proxy-Authorization\",\n\
    [\"range\"] = \"Range\",\n\
    [\"received\"] = \"Received\",\n\
    [\"received-from-mta\"] = \"Received-From-MTA\",\n\
    [\"references\"] = \"References\",\n\
    [\"referer\"] = \"Referer\",\n\
    [\"remote-mta\"] = \"Remote-MTA\",\n\
    [\"reply-to\"] = \"Reply-To\",\n\
    [\"reporting-mta\"] = \"Reporting-MTA\",\n\
    [\"resent-bcc\"] = \"Resent-Bcc\",\n\
    [\"resent-cc\"] = \"Resent-Cc\",\n\
    [\"resent-date\"] = \"Resent-Date\",\n\
    [\"resent-from\"] = \"Resent-From\",\n\
    [\"resent-message-id\"] = \"Resent-Message-ID\",\n\
    [\"resent-reply-to\"] = \"Resent-Reply-To\",\n\
    [\"resent-sender\"] = \"Resent-Sender\",\n\
    [\"resent-to\"] = \"Resent-To\",\n\
    [\"retry-after\"] = \"Retry-After\",\n\
    [\"return-path\"] = \"Return-Path\",\n\
    [\"sender\"] = \"Sender\",\n\
    [\"server\"] = \"Server\",\n\
    [\"smtp-remote-recipient\"] = \"SMTP-Remote-Recipient\",\n\
    [\"status\"] = \"Status\",\n\
    [\"subject\"] = \"Subject\",\n\
    [\"te\"] = \"TE\",\n\
    [\"to\"] = \"To\",\n\
    [\"trailer\"] = \"Trailer\",\n\
    [\"transfer-encoding\"] = \"Transfer-Encoding\",\n\
    [\"upgrade\"] = \"Upgrade\",\n\
    [\"user-agent\"] = \"User-Agent\",\n\
    [\"vary\"] = \"Vary\",\n\
    [\"via\"] = \"Via\",\n\
    [\"warning\"] = \"Warning\",\n\
    [\"will-retry-until\"] = \"Will-Retry-Until\",\n\
    [\"www-authenticate\"] = \"WWW-Authenticate\",\n\
    [\"x-mailer\"] = \"X-Mailer\",\n\
}\n\
\n\
return _M";
const char *luasocket_http_luafilesource = "-----------------------------------------------------------------------------\n\
-- HTTP/1.1 client support for the Lua language.\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module and import dependencies\n\
-------------------------------------------------------------------------------\n\
local socket = require(\"socket\")\n\
local url = require(\"socket.url\")\n\
local ltn12 = require(\"ltn12\")\n\
local mime = require(\"mime\")\n\
local string = require(\"string\")\n\
local headers = require(\"socket.headers\")\n\
local base = _G\n\
local table = require(\"table\")\n\
socket.http = {}\n\
local _M = socket.http\n\
\n\
-----------------------------------------------------------------------------\n\
-- Program constants\n\
-----------------------------------------------------------------------------\n\
-- connection timeout in seconds\n\
_M.TIMEOUT = 60\n\
-- user agent field sent in request\n\
_M.USERAGENT = socket._VERSION\n\
\n\
-- supported schemes and their particulars\n\
local SCHEMES = {\n\
    http = {\n\
        port = 80\n\
        , create = function(t)\n\
            return socket.tcp end }\n\
    , https = {\n\
        port = 443\n\
        , create = function(t)\n\
          local https = assert(\n\
            require(\"ssl.https\"), 'LuaSocket: LuaSec not found')\n\
          local tcp = assert(\n\
            https.tcp, 'LuaSocket: Function tcp() not available from LuaSec')\n\
          return tcp(t) end }}\n\
\n\
-- default scheme and port for document retrieval\n\
local SCHEME = 'http'\n\
local PORT = SCHEMES[SCHEME].port\n\
-----------------------------------------------------------------------------\n\
-- Reads MIME headers from a connection, unfolding where needed\n\
-----------------------------------------------------------------------------\n\
local function receiveheaders(sock, headers)\n\
    local line, name, value, err\n\
    headers = headers or {}\n\
    -- get first line\n\
    line, err = sock:receive()\n\
    if err then return nil, err end\n\
    -- headers go until a blank line is found\n\
    while line ~= \"\" do\n\
        -- get field-name and value\n\
        name, value = socket.skip(2, string.find(line, \"^(.-):%s*(.*)\"))\n\
        if not (name and value) then return nil, \"malformed reponse headers\" end\n\
        name = string.lower(name)\n\
        -- get next line (value might be folded)\n\
        line, err  = sock:receive()\n\
        if err then return nil, err end\n\
        -- unfold any folded values\n\
        while string.find(line, \"^%s\") do\n\
            value = value .. line\n\
            line = sock:receive()\n\
            if err then return nil, err end\n\
        end\n\
        -- save pair in table\n\
        if headers[name] then headers[name] = headers[name] .. \", \" .. value\n\
        else headers[name] = value end\n\
    end\n\
    return headers\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Extra sources and sinks\n\
-----------------------------------------------------------------------------\n\
socket.sourcet[\"http-chunked\"] = function(sock, headers)\n\
    return base.setmetatable({\n\
        getfd = function() return sock:getfd() end,\n\
        dirty = function() return sock:dirty() end\n\
    }, {\n\
        __call = function()\n\
            -- get chunk size, skip extention\n\
            local line, err = sock:receive()\n\
            if err then return nil, err end\n\
            local size = base.tonumber(string.gsub(line, \";.*\", \"\"), 16)\n\
            if not size then return nil, \"invalid chunk size\" end\n\
            -- was it the last chunk?\n\
            if size > 0 then\n\
                -- if not, get chunk and skip terminating CRLF\n\
                local chunk, err, part = sock:receive(size)\n\
                if chunk then sock:receive() end\n\
                return chunk, err\n\
            else\n\
                -- if it was, read trailers into headers table\n\
                headers, err = receiveheaders(sock, headers)\n\
                if not headers then return nil, err end\n\
            end\n\
        end\n\
    })\n\
end\n\
\n\
socket.sinkt[\"http-chunked\"] = function(sock)\n\
    return base.setmetatable({\n\
        getfd = function() return sock:getfd() end,\n\
        dirty = function() return sock:dirty() end\n\
    }, {\n\
        __call = function(self, chunk, err)\n\
            if not chunk then return sock:send(\"0\\r\\n\\r\\n\") end\n\
            local size = string.format(\"%X\\r\\n\", string.len(chunk))\n\
            return sock:send(size ..  chunk .. \"\\r\\n\")\n\
        end\n\
    })\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Low level HTTP API\n\
-----------------------------------------------------------------------------\n\
local metat = { __index = {} }\n\
\n\
function _M.open(host, port, create)\n\
    -- create socket with user connect function, or with default\n\
    local c = socket.try(create())\n\
    local h = base.setmetatable({ c = c }, metat)\n\
    -- create finalized try\n\
    h.try = socket.newtry(function() h:close() end)\n\
    -- set timeout before connecting\n\
    h.try(c:settimeout(_M.TIMEOUT))\n\
    h.try(c:connect(host, port))\n\
    -- here everything worked\n\
    return h\n\
end\n\
\n\
function metat.__index:sendrequestline(method, uri)\n\
    local reqline = string.format(\"%s %s HTTP/1.1\\r\\n\", method or \"GET\", uri)\n\
    return self.try(self.c:send(reqline))\n\
end\n\
\n\
function metat.__index:sendheaders(tosend)\n\
    local canonic = headers.canonic\n\
    local h = \"\\r\\n\"\n\
    for f, v in base.pairs(tosend) do\n\
        h = (canonic[f] or f) .. \": \" .. v .. \"\\r\\n\" .. h\n\
    end\n\
    self.try(self.c:send(h))\n\
    return 1\n\
end\n\
\n\
function metat.__index:sendbody(headers, source, step)\n\
    source = source or ltn12.source.empty()\n\
    step = step or ltn12.pump.step\n\
    -- if we don't know the size in advance, send chunked and hope for the best\n\
    local mode = \"http-chunked\"\n\
    if headers[\"content-length\"] then mode = \"keep-open\" end\n\
    return self.try(ltn12.pump.all(source, socket.sink(mode, self.c), step))\n\
end\n\
\n\
function metat.__index:receivestatusline()\n\
    local status,ec = self.try(self.c:receive(5))\n\
    -- identify HTTP/0.9 responses, which do not contain a status line\n\
    -- this is just a heuristic, but is what the RFC recommends\n\
    if status ~= \"HTTP/\" then\n\
        if ec == \"timeout\" then\n\
            return 408\n\
        end \n\
        return nil, status \n\
    end\n\
    -- otherwise proceed reading a status line\n\
    status = self.try(self.c:receive(\"*l\", status))\n\
    local code = socket.skip(2, string.find(status, \"HTTP/%d*%.%d* (%d%d%d)\"))\n\
    return self.try(base.tonumber(code), status)\n\
end\n\
\n\
function metat.__index:receiveheaders()\n\
    return self.try(receiveheaders(self.c))\n\
end\n\
\n\
function metat.__index:receivebody(headers, sink, step)\n\
    sink = sink or ltn12.sink.null()\n\
    step = step or ltn12.pump.step\n\
    local length = base.tonumber(headers[\"content-length\"])\n\
    local t = headers[\"transfer-encoding\"] -- shortcut\n\
    local mode = \"default\" -- connection close\n\
    if t and t ~= \"identity\" then mode = \"http-chunked\"\n\
    elseif base.tonumber(headers[\"content-length\"]) then mode = \"by-length\" end\n\
    return self.try(ltn12.pump.all(socket.source(mode, self.c, length),\n\
        sink, step))\n\
end\n\
\n\
function metat.__index:receive09body(status, sink, step)\n\
    local source = ltn12.source.rewind(socket.source(\"until-closed\", self.c))\n\
    source(status)\n\
    return self.try(ltn12.pump.all(source, sink, step))\n\
end\n\
\n\
function metat.__index:close()\n\
    return self.c:close()\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- High level HTTP API\n\
-----------------------------------------------------------------------------\n\
local function adjusturi(reqt)\n\
    local u = reqt\n\
    -- if there is a proxy, we need the full url. otherwise, just a part.\n\
    if not reqt.proxy and not _M.PROXY then\n\
        u = {\n\
           path = socket.try(reqt.path, \"invalid path 'nil'\"),\n\
           params = reqt.params,\n\
           query = reqt.query,\n\
           fragment = reqt.fragment\n\
        }\n\
    end\n\
    return url.build(u)\n\
end\n\
\n\
local function adjustproxy(reqt)\n\
    local proxy = reqt.proxy or _M.PROXY\n\
    if proxy then\n\
        proxy = url.parse(proxy)\n\
        return proxy.host, proxy.port or 3128\n\
    else\n\
        return reqt.host, reqt.port\n\
    end\n\
end\n\
\n\
local function adjustheaders(reqt)\n\
    -- default headers\n\
    local host = reqt.host\n\
    local port = tostring(reqt.port)\n\
    if port ~= tostring(SCHEMES[reqt.scheme].port) then\n\
        host = host .. ':' .. port end\n\
    local lower = {\n\
        [\"user-agent\"] = _M.USERAGENT,\n\
        [\"host\"] = host,\n\
        [\"connection\"] = \"close, TE\",\n\
        [\"te\"] = \"trailers\"\n\
    }\n\
    -- if we have authentication information, pass it along\n\
    if reqt.user and reqt.password then\n\
        lower[\"authorization\"] =\n\
            \"Basic \" ..  (mime.b64(reqt.user .. \":\" ..\n\
        url.unescape(reqt.password)))\n\
    end\n\
    -- if we have proxy authentication information, pass it along\n\
    local proxy = reqt.proxy or _M.PROXY\n\
    if proxy then\n\
        proxy = url.parse(proxy)\n\
        if proxy.user and proxy.password then\n\
            lower[\"proxy-authorization\"] =\n\
                \"Basic \" ..  (mime.b64(proxy.user .. \":\" .. proxy.password))\n\
        end\n\
    end\n\
    -- override with user headers\n\
    for i,v in base.pairs(reqt.headers or lower) do\n\
        lower[string.lower(i)] = v\n\
    end\n\
    return lower\n\
end\n\
\n\
-- default url parts\n\
local default = {\n\
    path =\"/\"\n\
    , scheme = \"http\"\n\
}\n\
\n\
local function adjustrequest(reqt)\n\
    -- parse url if provided\n\
    local nreqt = reqt.url and url.parse(reqt.url, default) or {}\n\
    -- explicit components override url\n\
    for i,v in base.pairs(reqt) do nreqt[i] = v end\n\
    -- default to scheme particulars\n\
    local schemedefs, host, port, method\n\
        = SCHEMES[nreqt.scheme], nreqt.host, nreqt.port, nreqt.method\n\
    if not nreqt.create then nreqt.create = schemedefs.create(nreqt) end\n\
    if not (port and port ~= '') then nreqt.port = schemedefs.port end\n\
    if not (method and method ~= '') then nreqt.method = 'GET' end\n\
    if not (host and host ~= \"\") then\n\
        socket.try(nil, \"invalid host '\" .. base.tostring(nreqt.host) .. \"'\")\n\
    end\n\
    -- compute uri if user hasn't overriden\n\
    nreqt.uri = reqt.uri or adjusturi(nreqt)\n\
    -- adjust headers in request\n\
    nreqt.headers = adjustheaders(nreqt)\n\
    -- ajust host and port if there is a proxy\n\
    nreqt.host, nreqt.port = adjustproxy(nreqt)\n\
    return nreqt\n\
end\n\
\n\
local function shouldredirect(reqt, code, headers)\n\
    local location = headers.location\n\
    if not location then return false end\n\
    location = string.gsub(location, \"%s\", \"\")\n\
    if location == \"\" then return false end\n\
    local scheme = url.parse(location).scheme\n\
    if scheme and (not SCHEMES[scheme]) then return false end\n\
    -- avoid https downgrades\n\
    if ('https' == reqt.scheme) and ('https' ~= scheme) then return false end\n\
    return (reqt.redirect ~= false) and\n\
           (code == 301 or code == 302 or code == 303 or code == 307) and\n\
           (not reqt.method or reqt.method == \"GET\" or reqt.method == \"HEAD\")\n\
        and ((false == reqt.maxredirects)\n\
                or ((reqt.nredirects or 0)\n\
                        < (reqt.maxredirects or 5)))\n\
end\n\
\n\
local function shouldreceivebody(reqt, code)\n\
    if reqt.method == \"HEAD\" then return nil end\n\
    if code == 204 or code == 304 then return nil end\n\
    if code >= 100 and code < 200 then return nil end\n\
    return 1\n\
end\n\
\n\
-- forward declarations\n\
local trequest, tredirect\n\
\n\
--[[local]] function tredirect(reqt, location)\n\
    -- the RFC says the redirect URL has to be absolute, but some\n\
    -- servers do not respect that\n\
    local newurl = url.absolute(reqt.url, location)\n\
    -- if switching schemes, reset port and create function\n\
    if url.parse(newurl).scheme ~= reqt.scheme then\n\
        reqt.port = nil\n\
        reqt.create = nil end\n\
    -- make new request\n\
    local result, code, headers, status = trequest {\n\
        url = newurl,\n\
        source = reqt.source,\n\
        sink = reqt.sink,\n\
        headers = reqt.headers,\n\
        proxy = reqt.proxy,\n\
        maxredirects = reqt.maxredirects,\n\
        nredirects = (reqt.nredirects or 0) + 1,\n\
        create = reqt.create\n\
    }\n\
    -- pass location header back as a hint we redirected\n\
    headers = headers or {}\n\
    headers.location = headers.location or location\n\
    return result, code, headers, status\n\
end\n\
\n\
--[[local]] function trequest(reqt)\n\
    -- we loop until we get what we want, or\n\
    -- until we are sure there is no way to get it\n\
    local nreqt = adjustrequest(reqt)\n\
    local h = _M.open(nreqt.host, nreqt.port, nreqt.create)\n\
    -- send request line and headers\n\
    h:sendrequestline(nreqt.method, nreqt.uri)\n\
    h:sendheaders(nreqt.headers)\n\
    -- if there is a body, send it\n\
    if nreqt.source then\n\
        h:sendbody(nreqt.headers, nreqt.source, nreqt.step)\n\
    end\n\
    local code, status = h:receivestatusline()\n\
    -- if it is an HTTP/0.9 server, simply get the body and we are done\n\
    if not code then\n\
        h:receive09body(status, nreqt.sink, nreqt.step)\n\
        return 1, 200\n\
    elseif code == 408 then\n\
        return 1, code\n\
    end\n\
    local headers\n\
    -- ignore any 100-continue messages\n\
    while code == 100 do\n\
        headers = h:receiveheaders()\n\
        code, status = h:receivestatusline()\n\
    end\n\
    headers = h:receiveheaders()\n\
    -- at this point we should have a honest reply from the server\n\
    -- we can't redirect if we already used the source, so we report the error\n\
    if shouldredirect(nreqt, code, headers) and not nreqt.source then\n\
        h:close()\n\
        return tredirect(reqt, headers.location)\n\
    end\n\
    -- here we are finally done\n\
    if shouldreceivebody(nreqt, code) then\n\
        h:receivebody(headers, nreqt.sink, nreqt.step)\n\
    end\n\
    h:close()\n\
    return 1, code, headers, status\n\
end\n\
\n\
-- turns an url and a body into a generic request\n\
local function genericform(u, b)\n\
    local t = {}\n\
    local reqt = {\n\
        url = u,\n\
        sink = ltn12.sink.table(t),\n\
        target = t\n\
    }\n\
    if b then\n\
        reqt.source = ltn12.source.string(b)\n\
        reqt.headers = {\n\
            [\"content-length\"] = string.len(b),\n\
            [\"content-type\"] = \"application/x-www-form-urlencoded\"\n\
        }\n\
        reqt.method = \"POST\"\n\
    end\n\
    return reqt\n\
end\n\
\n\
_M.genericform = genericform\n\
\n\
local function srequest(u, b)\n\
    local reqt = genericform(u, b)\n\
    local _, code, headers, status = trequest(reqt)\n\
    return table.concat(reqt.target), code, headers, status\n\
end\n\
\n\
_M.request = socket.protect(function(reqt, body)\n\
    if base.type(reqt) == \"string\" then return srequest(reqt, body)\n\
    else return trequest(reqt) end\n\
end)\n\
\n\
_M.schemes = SCHEMES\n\
return _M\n\
";
const char *luasocket_ltn12_luafilesource = "-----------------------------------------------------------------------------\n\
-- LTN12 - Filters, sources, sinks and pumps.\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module\n\
-----------------------------------------------------------------------------\n\
local string = require(\"string\")\n\
local table = require(\"table\")\n\
local unpack = unpack or table.unpack\n\
local base = _G\n\
local _M = {}\n\
if module then -- heuristic for exporting a global package table\n\
    ltn12 = _M\n\
end\n\
local filter,source,sink,pump = {},{},{},{}\n\
\n\
_M.filter = filter\n\
_M.source = source\n\
_M.sink = sink\n\
_M.pump = pump\n\
\n\
local unpack = unpack or table.unpack\n\
local select = base.select\n\
\n\
-- 2048 seems to be better in windows...\n\
_M.BLOCKSIZE = 2048\n\
_M._VERSION = \"LTN12 1.0.3\"\n\
\n\
-----------------------------------------------------------------------------\n\
-- Filter stuff\n\
-----------------------------------------------------------------------------\n\
-- returns a high level filter that cycles a low-level filter\n\
function filter.cycle(low, ctx, extra)\n\
    base.assert(low)\n\
    return function(chunk)\n\
        local ret\n\
        ret, ctx = low(ctx, chunk, extra)\n\
        return ret\n\
    end\n\
end\n\
\n\
-- chains a bunch of filters together\n\
-- (thanks to Wim Couwenberg)\n\
function filter.chain(...)\n\
    local arg = {...}\n\
    local n = base.select('#',...)\n\
    local top, index = 1, 1\n\
    local retry = \"\"\n\
    return function(chunk)\n\
        retry = chunk and retry\n\
        while true do\n\
            if index == top then\n\
                chunk = arg[index](chunk)\n\
                if chunk == \"\" or top == n then return chunk\n\
                elseif chunk then index = index + 1\n\
                else\n\
                    top = top+1\n\
                    index = top\n\
                end\n\
            else\n\
                chunk = arg[index](chunk or \"\")\n\
                if chunk == \"\" then\n\
                    index = index - 1\n\
                    chunk = retry\n\
                elseif chunk then\n\
                    if index == n then return chunk\n\
                    else index = index + 1 end\n\
                else base.error(\"filter returned inappropriate nil\") end\n\
            end\n\
        end\n\
    end\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Source stuff\n\
-----------------------------------------------------------------------------\n\
-- create an empty source\n\
local function empty()\n\
    return nil\n\
end\n\
\n\
function source.empty()\n\
    return empty\n\
end\n\
\n\
-- returns a source that just outputs an error\n\
function source.error(err)\n\
    return function()\n\
        return nil, err\n\
    end\n\
end\n\
\n\
-- creates a file source\n\
function source.file(handle, io_err)\n\
    if handle then\n\
        return function()\n\
            local chunk = handle:read(_M.BLOCKSIZE)\n\
            if not chunk then handle:close() end\n\
            return chunk\n\
        end\n\
    else return source.error(io_err or \"unable to open file\") end\n\
end\n\
\n\
-- turns a fancy source into a simple source\n\
function source.simplify(src)\n\
    base.assert(src)\n\
    return function()\n\
        local chunk, err_or_new = src()\n\
        src = err_or_new or src\n\
        if not chunk then return nil, err_or_new\n\
        else return chunk end\n\
    end\n\
end\n\
\n\
-- creates string source\n\
function source.string(s)\n\
    if s then\n\
        local i = 1\n\
        return function()\n\
            local chunk = string.sub(s, i, i+_M.BLOCKSIZE-1)\n\
            i = i + _M.BLOCKSIZE\n\
            if chunk ~= \"\" then return chunk\n\
            else return nil end\n\
        end\n\
    else return source.empty() end\n\
end\n\
\n\
-- creates table source\n\
function source.table(t)\n\
    base.assert('table' == type(t))\n\
    local i = 0\n\
    return function()\n\
        i = i + 1\n\
        return t[i]\n\
    end\n\
end\n\
\n\
-- creates rewindable source\n\
function source.rewind(src)\n\
    base.assert(src)\n\
    local t = {}\n\
    return function(chunk)\n\
        if not chunk then\n\
            chunk = table.remove(t)\n\
            if not chunk then return src()\n\
            else return chunk end\n\
        else\n\
            table.insert(t, chunk)\n\
        end\n\
    end\n\
end\n\
\n\
-- chains a source with one or several filter(s)\n\
function source.chain(src, f, ...)\n\
    if ... then f=filter.chain(f, ...) end\n\
    base.assert(src and f)\n\
    local last_in, last_out = \"\", \"\"\n\
    local state = \"feeding\"\n\
    local err\n\
    return function()\n\
        if not last_out then\n\
            base.error('source is empty!', 2)\n\
        end\n\
        while true do\n\
            if state == \"feeding\" then\n\
                last_in, err = src()\n\
                if err then return nil, err end\n\
                last_out = f(last_in)\n\
                if not last_out then\n\
                    if last_in then\n\
                        base.error('filter returned inappropriate nil')\n\
                    else\n\
                        return nil\n\
                    end\n\
                elseif last_out ~= \"\" then\n\
                    state = \"eating\"\n\
                    if last_in then last_in = \"\" end\n\
                    return last_out\n\
                end\n\
            else\n\
                last_out = f(last_in)\n\
                if last_out == \"\" then\n\
                    if last_in == \"\" then\n\
                        state = \"feeding\"\n\
                    else\n\
                        base.error('filter returned \"\"')\n\
                    end\n\
                elseif not last_out then\n\
                    if last_in then\n\
                        base.error('filter returned inappropriate nil')\n\
                    else\n\
                        return nil\n\
                    end\n\
                else\n\
                    return last_out\n\
                end\n\
            end\n\
        end\n\
    end\n\
end\n\
\n\
-- creates a source that produces contents of several sources, one after the\n\
-- other, as if they were concatenated\n\
-- (thanks to Wim Couwenberg)\n\
function source.cat(...)\n\
    local arg = {...}\n\
    local src = table.remove(arg, 1)\n\
    return function()\n\
        while src do\n\
            local chunk, err = src()\n\
            if chunk then return chunk end\n\
            if err then return nil, err end\n\
            src = table.remove(arg, 1)\n\
        end\n\
    end\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Sink stuff\n\
-----------------------------------------------------------------------------\n\
-- creates a sink that stores into a table\n\
function sink.table(t)\n\
    t = t or {}\n\
    local f = function(chunk, err)\n\
        if chunk then table.insert(t, chunk) end\n\
        return 1\n\
    end\n\
    return f, t\n\
end\n\
\n\
-- turns a fancy sink into a simple sink\n\
function sink.simplify(snk)\n\
    base.assert(snk)\n\
    return function(chunk, err)\n\
        local ret, err_or_new = snk(chunk, err)\n\
        if not ret then return nil, err_or_new end\n\
        snk = err_or_new or snk\n\
        return 1\n\
    end\n\
end\n\
\n\
-- creates a file sink\n\
function sink.file(handle, io_err)\n\
    if handle then\n\
        return function(chunk, err)\n\
            if not chunk then\n\
                handle:close()\n\
                return 1\n\
            else return handle:write(chunk) end\n\
        end\n\
    else return sink.error(io_err or \"unable to open file\") end\n\
end\n\
\n\
-- creates a sink that discards data\n\
local function null()\n\
    return 1\n\
end\n\
\n\
function sink.null()\n\
    return null\n\
end\n\
\n\
-- creates a sink that just returns an error\n\
function sink.error(err)\n\
    return function()\n\
        return nil, err\n\
    end\n\
end\n\
\n\
-- chains a sink with one or several filter(s)\n\
function sink.chain(f, snk, ...)\n\
    if ... then\n\
        local args = { f, snk, ... }\n\
        snk = table.remove(args, #args)\n\
        f = filter.chain(unpack(args))\n\
    end\n\
    base.assert(f and snk)\n\
    return function(chunk, err)\n\
        if chunk ~= \"\" then\n\
            local filtered = f(chunk)\n\
            local done = chunk and \"\"\n\
            while true do\n\
                local ret, snkerr = snk(filtered, err)\n\
                if not ret then return nil, snkerr end\n\
                if filtered == done then return 1 end\n\
                filtered = f(done)\n\
            end\n\
        else return 1 end\n\
    end\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Pump stuff\n\
-----------------------------------------------------------------------------\n\
-- pumps one chunk from the source to the sink\n\
function pump.step(src, snk)\n\
    local chunk, src_err = src()\n\
    local ret, snk_err = snk(chunk, src_err)\n\
    if chunk and ret then return 1\n\
    else return nil, src_err or snk_err end\n\
end\n\
\n\
-- pumps all data from a source to a sink, using a step function\n\
function pump.all(src, snk, step)\n\
    base.assert(src and snk)\n\
    step = step or pump.step\n\
    while true do\n\
        local ret, err = step(src, snk)\n\
        if not ret then\n\
            if err then return nil, err\n\
            else return 1 end\n\
        end\n\
    end\n\
end\n\
\n\
return _M\n\
";
const char *luasocket_mbox_luafilesource = "local _M = {}\n\
\n\
if module then\n\
    mbox = _M\n\
end \n\
\n\
function _M.split_message(message_s)\n\
    local message = {}\n\
    message_s = string.gsub(message_s, \"\\r\\n\", \"\\n\")\n\
    string.gsub(message_s, \"^(.-\\n)\\n\", function (h) message.headers = h end)\n\
    string.gsub(message_s, \"^.-\\n\\n(.*)\", function (b) message.body = b end)\n\
    if not message.body then\n\
        string.gsub(message_s, \"^\\n(.*)\", function (b) message.body = b end)\n\
    end\n\
    if not message.headers and not message.body then\n\
        message.headers = message_s\n\
    end\n\
    return message.headers or \"\", message.body or \"\"\n\
end\n\
\n\
function _M.split_headers(headers_s)\n\
    local headers = {}\n\
    headers_s = string.gsub(headers_s, \"\\r\\n\", \"\\n\")\n\
    headers_s = string.gsub(headers_s, \"\\n[ ]+\", \" \")\n\
    string.gsub(\"\\n\" .. headers_s, \"\\n([^\\n]+)\", function (h) table.insert(headers, h) end)\n\
    return headers\n\
end\n\
\n\
function _M.parse_header(header_s)\n\
    header_s = string.gsub(header_s, \"\\n[ ]+\", \" \")\n\
    header_s = string.gsub(header_s, \"\\n+\", \"\")\n\
    local _, __, name, value = string.find(header_s, \"([^%s:]-):%s*(.*)\")\n\
    return name, value\n\
end\n\
\n\
function _M.parse_headers(headers_s)\n\
    local headers_t = _M.split_headers(headers_s)\n\
    local headers = {}\n\
    for i = 1, #headers_t do\n\
        local name, value = _M.parse_header(headers_t[i])\n\
        if name then\n\
            name = string.lower(name)\n\
            if headers[name] then\n\
                headers[name] = headers[name] .. \", \" .. value\n\
            else headers[name] = value end\n\
        end\n\
    end\n\
    return headers\n\
end\n\
\n\
function _M.parse_from(from)\n\
    local _, __, name, address = string.find(from, \"^%s*(.-)%s*%<(.-)%>\")\n\
    if not address then\n\
        _, __, address = string.find(from, \"%s*(.+)%s*\")\n\
    end\n\
    name = name or \"\"\n\
    address = address or \"\"\n\
    if name == \"\" then name = address end\n\
    name = string.gsub(name, '\"', \"\")\n\
    return name, address\n\
end\n\
\n\
function _M.split_mbox(mbox_s)\n\
    local mbox = {}\n\
    mbox_s = string.gsub(mbox_s, \"\\r\\n\", \"\\n\") ..\"\\n\\nFrom \\n\"\n\
    local nj, i, j = 1, 1, 1\n\
    while 1 do\n\
        i, nj = string.find(mbox_s, \"\\n\\nFrom .-\\n\", j)\n\
        if not i then break end\n\
        local message = string.sub(mbox_s, j, i-1)\n\
        table.insert(mbox, message)\n\
        j = nj+1\n\
    end\n\
    return mbox\n\
end\n\
\n\
function _M.parse(mbox_s)\n\
    local mbox = _M.split_mbox(mbox_s)\n\
    for i = 1, #mbox do\n\
        mbox[i] = _M.parse_message(mbox[i])\n\
    end\n\
    return mbox\n\
end\n\
\n\
function _M.parse_message(message_s)\n\
    local message = {}\n\
    message.headers, message.body = _M.split_message(message_s)\n\
    message.headers = _M.parse_headers(message.headers)\n\
    return message\n\
end\n\
\n\
return _M\n\
";
const char *luasocket_mime_luafilesource = "-----------------------------------------------------------------------------\n\
-- MIME support for the Lua language.\n\
-- Author: Diego Nehab\n\
-- Conforming to RFCs 2045-2049\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module and import dependencies\n\
-----------------------------------------------------------------------------\n\
local base = _G\n\
local ltn12 = require(\"ltn12\")\n\
local mime = require(\"mime.core\")\n\
local string = require(\"string\")\n\
local _M = mime\n\
\n\
-- encode, decode and wrap algorithm tables\n\
local encodet, decodet, wrapt = {},{},{}\n\
\n\
_M.encodet = encodet\n\
_M.decodet = decodet\n\
_M.wrapt   = wrapt  \n\
\n\
-- creates a function that chooses a filter by name from a given table\n\
local function choose(table)\n\
    return function(name, opt1, opt2)\n\
        if base.type(name) ~= \"string\" then\n\
            name, opt1, opt2 = \"default\", name, opt1\n\
        end\n\
        local f = table[name or \"nil\"]\n\
        if not f then \n\
            base.error(\"unknown key (\" .. base.tostring(name) .. \")\", 3)\n\
        else return f(opt1, opt2) end\n\
    end\n\
end\n\
\n\
-- define the encoding filters\n\
encodet['base64'] = function()\n\
    return ltn12.filter.cycle(_M.b64, \"\")\n\
end\n\
\n\
encodet['quoted-printable'] = function(mode)\n\
    return ltn12.filter.cycle(_M.qp, \"\",\n\
        (mode == \"binary\") and \"=0D=0A\" or \"\\r\\n\")\n\
end\n\
\n\
-- define the decoding filters\n\
decodet['base64'] = function()\n\
    return ltn12.filter.cycle(_M.unb64, \"\")\n\
end\n\
\n\
decodet['quoted-printable'] = function()\n\
    return ltn12.filter.cycle(_M.unqp, \"\")\n\
end\n\
\n\
local function format(chunk)\n\
    if chunk then\n\
        if chunk == \"\" then return \"''\"\n\
        else return string.len(chunk) end\n\
    else return \"nil\" end\n\
end\n\
\n\
-- define the line-wrap filters\n\
wrapt['text'] = function(length)\n\
    length = length or 76\n\
    return ltn12.filter.cycle(_M.wrp, length, length)\n\
end\n\
wrapt['base64'] = wrapt['text']\n\
wrapt['default'] = wrapt['text']\n\
\n\
wrapt['quoted-printable'] = function()\n\
    return ltn12.filter.cycle(_M.qpwrp, 76, 76)\n\
end\n\
\n\
-- function that choose the encoding, decoding or wrap algorithm\n\
_M.encode = choose(encodet)\n\
_M.decode = choose(decodet)\n\
_M.wrap = choose(wrapt)\n\
\n\
-- define the end-of-line normalization filter\n\
function _M.normalize(marker)\n\
    return ltn12.filter.cycle(_M.eol, 0, marker)\n\
end\n\
\n\
-- high level stuffing filter\n\
function _M.stuff()\n\
    return ltn12.filter.cycle(_M.dot, 2)\n\
end\n\
\n\
return _M\n\
";
const char *luasocket_smtp_luafilesource = "-----------------------------------------------------------------------------\n\
-- SMTP client support for the Lua language.\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module and import dependencies\n\
-----------------------------------------------------------------------------\n\
local base = _G\n\
local coroutine = require(\"coroutine\")\n\
local string = require(\"string\")\n\
local math = require(\"math\")\n\
local os = require(\"os\")\n\
local socket = require(\"socket\")\n\
local tp = require(\"socket.tp\")\n\
local ltn12 = require(\"ltn12\")\n\
local headers = require(\"socket.headers\")\n\
local mime = require(\"mime\")\n\
\n\
socket.smtp = {}\n\
local _M = socket.smtp\n\
\n\
-----------------------------------------------------------------------------\n\
-- Program constants\n\
-----------------------------------------------------------------------------\n\
-- timeout for connection\n\
_M.TIMEOUT = 60\n\
-- default server used to send e-mails\n\
_M.SERVER = \"localhost\"\n\
-- default port\n\
_M.PORT = 25\n\
-- domain used in HELO command and default sendmail\n\
-- If we are under a CGI, try to get from environment\n\
_M.DOMAIN = os.getenv(\"SERVER_NAME\") or \"localhost\"\n\
-- default time zone (means we don't know)\n\
_M.ZONE = \"-0000\"\n\
\n\
---------------------------------------------------------------------------\n\
-- Low level SMTP API\n\
-----------------------------------------------------------------------------\n\
local metat = { __index = {} }\n\
\n\
function metat.__index:greet(domain)\n\
    self.try(self.tp:check(\"2..\"))\n\
    self.try(self.tp:command(\"EHLO\", domain or _M.DOMAIN))\n\
    return socket.skip(1, self.try(self.tp:check(\"2..\")))\n\
end\n\
\n\
function metat.__index:mail(from)\n\
    self.try(self.tp:command(\"MAIL\", \"FROM:\" .. from))\n\
    return self.try(self.tp:check(\"2..\"))\n\
end\n\
\n\
function metat.__index:rcpt(to)\n\
    self.try(self.tp:command(\"RCPT\", \"TO:\" .. to))\n\
    return self.try(self.tp:check(\"2..\"))\n\
end\n\
\n\
function metat.__index:data(src, step)\n\
    self.try(self.tp:command(\"DATA\"))\n\
    self.try(self.tp:check(\"3..\"))\n\
    self.try(self.tp:source(src, step))\n\
    self.try(self.tp:send(\"\\r\\n.\\r\\n\"))\n\
    return self.try(self.tp:check(\"2..\"))\n\
end\n\
\n\
function metat.__index:quit()\n\
    self.try(self.tp:command(\"QUIT\"))\n\
    return self.try(self.tp:check(\"2..\"))\n\
end\n\
\n\
function metat.__index:close()\n\
    return self.tp:close()\n\
end\n\
\n\
function metat.__index:login(user, password)\n\
    self.try(self.tp:command(\"AUTH\", \"LOGIN\"))\n\
    self.try(self.tp:check(\"3..\"))\n\
    self.try(self.tp:send(mime.b64(user) .. \"\\r\\n\"))\n\
    self.try(self.tp:check(\"3..\"))\n\
    self.try(self.tp:send(mime.b64(password) .. \"\\r\\n\"))\n\
    return self.try(self.tp:check(\"2..\"))\n\
end\n\
\n\
function metat.__index:plain(user, password)\n\
    local auth = \"PLAIN \" .. mime.b64(\"\\0\" .. user .. \"\\0\" .. password)\n\
    self.try(self.tp:command(\"AUTH\", auth))\n\
    return self.try(self.tp:check(\"2..\"))\n\
end\n\
\n\
function metat.__index:auth(user, password, ext)\n\
    if not user or not password then return 1 end\n\
    if string.find(ext, \"AUTH[^\\n]+LOGIN\") then\n\
        return self:login(user, password)\n\
    elseif string.find(ext, \"AUTH[^\\n]+PLAIN\") then\n\
        return self:plain(user, password)\n\
    else\n\
        self.try(nil, \"authentication not supported\")\n\
    end\n\
end\n\
\n\
-- send message or throw an exception\n\
function metat.__index:send(mailt)\n\
    self:mail(mailt.from)\n\
    if base.type(mailt.rcpt) == \"table\" then\n\
        for i,v in base.ipairs(mailt.rcpt) do\n\
            self:rcpt(v)\n\
        end\n\
    else\n\
        self:rcpt(mailt.rcpt)\n\
    end\n\
    self:data(ltn12.source.chain(mailt.source, mime.stuff()), mailt.step)\n\
end\n\
\n\
function _M.open(server, port, create)\n\
    local tp = socket.try(tp.connect(server or _M.SERVER, port or _M.PORT,\n\
        _M.TIMEOUT, create))\n\
    local s = base.setmetatable({tp = tp}, metat)\n\
    -- make sure tp is closed if we get an exception\n\
    s.try = socket.newtry(function()\n\
        s:close()\n\
    end)\n\
    return s\n\
end\n\
\n\
-- convert headers to lowercase\n\
local function lower_headers(headers)\n\
    local lower = {}\n\
    for i,v in base.pairs(headers or lower) do\n\
        lower[string.lower(i)] = v\n\
    end\n\
    return lower\n\
end\n\
\n\
---------------------------------------------------------------------------\n\
-- Multipart message source\n\
-----------------------------------------------------------------------------\n\
-- returns a hopefully unique mime boundary\n\
local seqno = 0\n\
local function newboundary()\n\
    seqno = seqno + 1\n\
    return string.format('%s%05d==%05u', os.date('%d%m%Y%H%M%S'),\n\
        math.random(0, 99999), seqno)\n\
end\n\
\n\
-- send_message forward declaration\n\
local send_message\n\
\n\
-- yield the headers all at once, it's faster\n\
local function send_headers(tosend)\n\
    local canonic = headers.canonic\n\
    local h = \"\\r\\n\"\n\
    for f,v in base.pairs(tosend) do\n\
        h = (canonic[f] or f) .. ': ' .. v .. \"\\r\\n\" .. h\n\
    end\n\
    coroutine.yield(h)\n\
end\n\
\n\
-- yield multipart message body from a multipart message table\n\
local function send_multipart(mesgt)\n\
    -- make sure we have our boundary and send headers\n\
    local bd = newboundary()\n\
    local headers = lower_headers(mesgt.headers or {})\n\
    headers['content-type'] = headers['content-type'] or 'multipart/mixed'\n\
    headers['content-type'] = headers['content-type'] ..\n\
        '; boundary=\"' ..  bd .. '\"'\n\
    send_headers(headers)\n\
    -- send preamble\n\
    if mesgt.body.preamble then\n\
        coroutine.yield(mesgt.body.preamble)\n\
        coroutine.yield(\"\\r\\n\")\n\
    end\n\
    -- send each part separated by a boundary\n\
    for i, m in base.ipairs(mesgt.body) do\n\
        coroutine.yield(\"\\r\\n--\" .. bd .. \"\\r\\n\")\n\
        send_message(m)\n\
    end\n\
    -- send last boundary\n\
    coroutine.yield(\"\\r\\n--\" .. bd .. \"--\\r\\n\\r\\n\")\n\
    -- send epilogue\n\
    if mesgt.body.epilogue then\n\
        coroutine.yield(mesgt.body.epilogue)\n\
        coroutine.yield(\"\\r\\n\")\n\
    end\n\
end\n\
\n\
-- yield message body from a source\n\
local function send_source(mesgt)\n\
    -- make sure we have a content-type\n\
    local headers = lower_headers(mesgt.headers or {})\n\
    headers['content-type'] = headers['content-type'] or\n\
        'text/plain; charset=\"iso-8859-1\"'\n\
    send_headers(headers)\n\
    -- send body from source\n\
    while true do\n\
        local chunk, err = mesgt.body()\n\
        if err then coroutine.yield(nil, err)\n\
        elseif chunk then coroutine.yield(chunk)\n\
        else break end\n\
    end\n\
end\n\
\n\
-- yield message body from a string\n\
local function send_string(mesgt)\n\
    -- make sure we have a content-type\n\
    local headers = lower_headers(mesgt.headers or {})\n\
    headers['content-type'] = headers['content-type'] or\n\
        'text/plain; charset=\"iso-8859-1\"'\n\
    send_headers(headers)\n\
    -- send body from string\n\
    coroutine.yield(mesgt.body)\n\
end\n\
\n\
-- message source\n\
function send_message(mesgt)\n\
    if base.type(mesgt.body) == \"table\" then send_multipart(mesgt)\n\
    elseif base.type(mesgt.body) == \"function\" then send_source(mesgt)\n\
    else send_string(mesgt) end\n\
end\n\
\n\
-- set defaul headers\n\
local function adjust_headers(mesgt)\n\
    local lower = lower_headers(mesgt.headers)\n\
    lower[\"date\"] = lower[\"date\"] or\n\
        os.date(\"!%a, %d %b %Y %H:%M:%S \") .. (mesgt.zone or _M.ZONE)\n\
    lower[\"x-mailer\"] = lower[\"x-mailer\"] or socket._VERSION\n\
    -- this can't be overriden\n\
    lower[\"mime-version\"] = \"1.0\"\n\
    return lower\n\
end\n\
\n\
function _M.message(mesgt)\n\
    mesgt.headers = adjust_headers(mesgt)\n\
    -- create and return message source\n\
    local co = coroutine.create(function() send_message(mesgt) end)\n\
    return function()\n\
        local ret, a, b = coroutine.resume(co)\n\
        if ret then return a, b\n\
        else return nil, a end\n\
    end\n\
end\n\
\n\
---------------------------------------------------------------------------\n\
-- High level SMTP API\n\
-----------------------------------------------------------------------------\n\
_M.send = socket.protect(function(mailt)\n\
    local s = _M.open(mailt.server, mailt.port, mailt.create)\n\
    local ext = s:greet(mailt.domain)\n\
    s:auth(mailt.user, mailt.password, ext)\n\
    s:send(mailt)\n\
    s:quit()\n\
    return s:close()\n\
end)\n\
\n\
return _M";
const char *luasocket_socket_luafilesource = "-----------------------------------------------------------------------------\n\
-- LuaSocket helper module\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module and import dependencies\n\
-----------------------------------------------------------------------------\n\
local base = _G\n\
local string = require(\"string\")\n\
local math = require(\"math\")\n\
local socket = require(\"socket.core\")\n\
\n\
local _M = socket\n\
\n\
-----------------------------------------------------------------------------\n\
-- Exported auxiliar functions\n\
-----------------------------------------------------------------------------\n\
function _M.connect4(address, port, laddress, lport)\n\
    return socket.connect(address, port, laddress, lport, \"inet\")\n\
end\n\
\n\
function _M.connect6(address, port, laddress, lport)\n\
    return socket.connect(address, port, laddress, lport, \"inet6\")\n\
end\n\
\n\
function _M.bind(host, port, backlog)\n\
    if host == \"*\" then host = \"0.0.0.0\" end\n\
    local addrinfo, err = socket.dns.getaddrinfo(host);\n\
    if not addrinfo then return nil, err end\n\
    local sock, res\n\
    err = \"no info on address\"\n\
    for i, alt in base.ipairs(addrinfo) do\n\
        if alt.family == \"inet\" then\n\
            sock, err = socket.tcp4()\n\
        else\n\
            sock, err = socket.tcp6()\n\
        end\n\
        if not sock then return nil, err end\n\
        sock:setoption(\"reuseaddr\", true)\n\
        res, err = sock:bind(alt.addr, port)\n\
        if not res then\n\
            sock:close()\n\
        else\n\
            res, err = sock:listen(backlog)\n\
            if not res then\n\
                sock:close()\n\
            else\n\
                return sock\n\
            end\n\
        end\n\
    end\n\
    return nil, err\n\
end\n\
\n\
_M.try = _M.newtry()\n\
\n\
function _M.choose(table)\n\
    return function(name, opt1, opt2)\n\
        if base.type(name) ~= \"string\" then\n\
            name, opt1, opt2 = \"default\", name, opt1\n\
        end\n\
        local f = table[name or \"nil\"]\n\
        if not f then base.error(\"unknown key (\".. base.tostring(name) ..\")\", 3)\n\
        else return f(opt1, opt2) end\n\
    end\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Socket sources and sinks, conforming to LTN12\n\
-----------------------------------------------------------------------------\n\
-- create namespaces inside LuaSocket namespace\n\
local sourcet, sinkt = {}, {}\n\
_M.sourcet = sourcet\n\
_M.sinkt = sinkt\n\
\n\
_M.BLOCKSIZE = 2048\n\
\n\
sinkt[\"close-when-done\"] = function(sock)\n\
    return base.setmetatable({\n\
        getfd = function() return sock:getfd() end,\n\
        dirty = function() return sock:dirty() end\n\
    }, {\n\
        __call = function(self, chunk, err)\n\
            if not chunk then\n\
                sock:close()\n\
                return 1\n\
            else return sock:send(chunk) end\n\
        end\n\
    })\n\
end\n\
\n\
sinkt[\"keep-open\"] = function(sock)\n\
    return base.setmetatable({\n\
        getfd = function() return sock:getfd() end,\n\
        dirty = function() return sock:dirty() end\n\
    }, {\n\
        __call = function(self, chunk, err)\n\
            if chunk then return sock:send(chunk)\n\
            else return 1 end\n\
        end\n\
    })\n\
end\n\
\n\
sinkt[\"default\"] = sinkt[\"keep-open\"]\n\
\n\
_M.sink = _M.choose(sinkt)\n\
\n\
sourcet[\"by-length\"] = function(sock, length)\n\
    return base.setmetatable({\n\
        getfd = function() return sock:getfd() end,\n\
        dirty = function() return sock:dirty() end\n\
    }, {\n\
        __call = function()\n\
            if length <= 0 then return nil end\n\
            local size = math.min(socket.BLOCKSIZE, length)\n\
            local chunk, err = sock:receive(size)\n\
            if err then return nil, err end\n\
            length = length - string.len(chunk)\n\
            return chunk\n\
        end\n\
    })\n\
end\n\
\n\
sourcet[\"until-closed\"] = function(sock)\n\
    local done\n\
    return base.setmetatable({\n\
        getfd = function() return sock:getfd() end,\n\
        dirty = function() return sock:dirty() end\n\
    }, {\n\
        __call = function()\n\
            if done then return nil end\n\
            local chunk, err, partial = sock:receive(socket.BLOCKSIZE)\n\
            if not err then return chunk\n\
            elseif err == \"closed\" then\n\
                sock:close()\n\
                done = 1\n\
                return partial\n\
            else return nil, err end\n\
        end\n\
    })\n\
end\n\
\n\
\n\
sourcet[\"default\"] = sourcet[\"until-closed\"]\n\
\n\
_M.source = _M.choose(sourcet)\n\
\n\
return _M\n\
";
const char *luasocket_tp_luafilesource = "-----------------------------------------------------------------------------\n\
-- Unified SMTP/FTP subsystem\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module and import dependencies\n\
-----------------------------------------------------------------------------\n\
local base = _G\n\
local string = require(\"string\")\n\
local socket = require(\"socket\")\n\
local ltn12 = require(\"ltn12\")\n\
\n\
socket.tp = {}\n\
local _M = socket.tp\n\
\n\
-----------------------------------------------------------------------------\n\
-- Program constants\n\
-----------------------------------------------------------------------------\n\
_M.TIMEOUT = 60\n\
\n\
-----------------------------------------------------------------------------\n\
-- Implementation\n\
-----------------------------------------------------------------------------\n\
-- gets server reply (works for SMTP and FTP)\n\
local function get_reply(c)\n\
    local code, current, sep\n\
    local line, err = c:receive()\n\
    local reply = line\n\
    if err then return nil, err end\n\
    code, sep = socket.skip(2, string.find(line, \"^(%d%d%d)(.?)\"))\n\
    if not code then return nil, \"invalid server reply\" end\n\
    if sep == \"-\" then -- reply is multiline\n\
        repeat\n\
            line, err = c:receive()\n\
            if err then return nil, err end\n\
            current, sep = socket.skip(2, string.find(line, \"^(%d%d%d)(.?)\"))\n\
            reply = reply .. \"\\n\" .. line\n\
        -- reply ends with same code\n\
        until code == current and sep == \" \"\n\
    end\n\
    return code, reply\n\
end\n\
\n\
-- metatable for sock object\n\
local metat = { __index = {} }\n\
\n\
function metat.__index:getpeername()\n\
    return self.c:getpeername()\n\
end\n\
\n\
function metat.__index:getsockname()\n\
    return self.c:getpeername()\n\
end\n\
\n\
function metat.__index:check(ok)\n\
    local code, reply = get_reply(self.c)\n\
    if not code then return nil, reply end\n\
    if base.type(ok) ~= \"function\" then\n\
        if base.type(ok) == \"table\" then\n\
            for i, v in base.ipairs(ok) do\n\
                if string.find(code, v) then\n\
                    return base.tonumber(code), reply\n\
                end\n\
            end\n\
            return nil, reply\n\
        else\n\
            if string.find(code, ok) then return base.tonumber(code), reply\n\
            else return nil, reply end\n\
        end\n\
    else return ok(base.tonumber(code), reply) end\n\
end\n\
\n\
function metat.__index:command(cmd, arg)\n\
    cmd = string.upper(cmd)\n\
    if arg then\n\
        return self.c:send(cmd .. \" \" .. arg.. \"\\r\\n\")\n\
    else\n\
        return self.c:send(cmd .. \"\\r\\n\")\n\
    end\n\
end\n\
\n\
function metat.__index:sink(snk, pat)\n\
    local chunk, err = self.c:receive(pat)\n\
    return snk(chunk, err)\n\
end\n\
\n\
function metat.__index:send(data)\n\
    return self.c:send(data)\n\
end\n\
\n\
function metat.__index:receive(pat)\n\
    return self.c:receive(pat)\n\
end\n\
\n\
function metat.__index:getfd()\n\
    return self.c:getfd()\n\
end\n\
\n\
function metat.__index:dirty()\n\
    return self.c:dirty()\n\
end\n\
\n\
function metat.__index:getcontrol()\n\
    return self.c\n\
end\n\
\n\
function metat.__index:source(source, step)\n\
    local sink = socket.sink(\"keep-open\", self.c)\n\
    local ret, err = ltn12.pump.all(source, sink, step or ltn12.pump.step)\n\
    return ret, err\n\
end\n\
\n\
-- closes the underlying c\n\
function metat.__index:close()\n\
    self.c:close()\n\
    return 1\n\
end\n\
\n\
-- connect with server and return c object\n\
function _M.connect(host, port, timeout, create)\n\
    local c, e = (create or socket.tcp)()\n\
    if not c then return nil, e end\n\
    c:settimeout(timeout or _M.TIMEOUT)\n\
    local r, e = c:connect(host, port)\n\
    if not r then\n\
        c:close()\n\
        return nil, e\n\
    end\n\
    return base.setmetatable({c = c}, metat)\n\
end\n\
\n\
return _M\n\
";
const char *luasocket_url_luafilesource = "-----------------------------------------------------------------------------\n\
-- URI parsing, composition and relative URL resolution\n\
-- LuaSocket toolkit.\n\
-- Author: Diego Nehab\n\
-----------------------------------------------------------------------------\n\
\n\
-----------------------------------------------------------------------------\n\
-- Declare module\n\
-----------------------------------------------------------------------------\n\
local string = require(\"string\")\n\
local base = _G\n\
local table = require(\"table\")\n\
local socket = require(\"socket\")\n\
\n\
socket.url = {}\n\
local _M = socket.url\n\
\n\
-----------------------------------------------------------------------------\n\
-- Module version\n\
-----------------------------------------------------------------------------\n\
_M._VERSION = \"URL 1.0.3\"\n\
\n\
-----------------------------------------------------------------------------\n\
-- Encodes a string into its escaped hexadecimal representation\n\
-- Input\n\
--   s: binary string to be encoded\n\
-- Returns\n\
--   escaped representation of string binary\n\
-----------------------------------------------------------------------------\n\
function _M.escape(s)\n\
    return (string.gsub(s, \"([^A-Za-z0-9_])\", function(c)\n\
        return string.format(\"%%%02x\", string.byte(c))\n\
    end))\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Protects a path segment, to prevent it from interfering with the\n\
-- url parsing.\n\
-- Input\n\
--   s: binary string to be encoded\n\
-- Returns\n\
--   escaped representation of string binary\n\
-----------------------------------------------------------------------------\n\
local function make_set(t)\n\
    local s = {}\n\
    for i,v in base.ipairs(t) do\n\
        s[t[i]] = 1\n\
    end\n\
    return s\n\
end\n\
\n\
-- these are allowed within a path segment, along with alphanum\n\
-- other characters must be escaped\n\
local segment_set = make_set {\n\
    \"-\", \"_\", \".\", \"!\", \"~\", \"*\", \"'\", \"(\",\n\
    \")\", \":\", \"@\", \"&\", \"=\", \"+\", \"$\", \",\",\n\
}\n\
\n\
local function protect_segment(s)\n\
    return string.gsub(s, \"([^A-Za-z0-9_])\", function (c)\n\
        if segment_set[c] then return c\n\
        else return string.format(\"%%%02X\", string.byte(c)) end\n\
    end)\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Unencodes a escaped hexadecimal string into its binary representation\n\
-- Input\n\
--   s: escaped hexadecimal string to be unencoded\n\
-- Returns\n\
--   unescaped binary representation of escaped hexadecimal  binary\n\
-----------------------------------------------------------------------------\n\
function _M.unescape(s)\n\
    return (string.gsub(s, \"%%(%x%x)\", function(hex)\n\
        return string.char(base.tonumber(hex, 16))\n\
    end))\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Removes '..' and '.' components appropriately from a path.\n\
-- Input\n\
--   path\n\
-- Returns\n\
--   dot-normalized path\n\
local function remove_dot_components(path)\n\
    local marker = string.char(1)\n\
    repeat\n\
        local was = path\n\
        path = path:gsub('//', '/'..marker..'/', 1)\n\
    until path == was\n\
    repeat\n\
        local was = path\n\
        path = path:gsub('/%./', '/', 1)\n\
    until path == was\n\
    repeat\n\
        local was = path\n\
        path = path:gsub('[^/]+/%.%./([^/]+)', '%1', 1)\n\
    until path == was\n\
    path = path:gsub('[^/]+/%.%./*$', '')\n\
    path = path:gsub('/%.%.$', '/')\n\
    path = path:gsub('/%.$', '/')\n\
    path = path:gsub('^/%.%./', '/')\n\
    path = path:gsub(marker, '')\n\
    return path\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Builds a path from a base path and a relative path\n\
-- Input\n\
--   base_path\n\
--   relative_path\n\
-- Returns\n\
--   corresponding absolute path\n\
-----------------------------------------------------------------------------\n\
local function absolute_path(base_path, relative_path)\n\
    if string.sub(relative_path, 1, 1) == \"/\" then\n\
      return remove_dot_components(relative_path) end\n\
    base_path = base_path:gsub(\"[^/]*$\", \"\")\n\
    if not base_path:find'/$' then base_path = base_path .. '/' end\n\
    local path = base_path .. relative_path\n\
    path = remove_dot_components(path)\n\
    return path\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Parses a url and returns a table with all its parts according to RFC 2396\n\
-- The following grammar describes the names given to the URL parts\n\
-- <url> ::= <scheme>://<authority>/<path>;<params>?<query>#<fragment>\n\
-- <authority> ::= <userinfo>@<host>:<port>\n\
-- <userinfo> ::= <user>[:<password>]\n\
-- <path> :: = {<segment>/}<segment>\n\
-- Input\n\
--   url: uniform resource locator of request\n\
--   default: table with default values for each field\n\
-- Returns\n\
--   table with the following fields, where RFC naming conventions have\n\
--   been preserved:\n\
--     scheme, authority, userinfo, user, password, host, port,\n\
--     path, params, query, fragment\n\
-- Obs:\n\
--   the leading '/' in {/<path>} is considered part of <path>\n\
-----------------------------------------------------------------------------\n\
function _M.parse(url, default)\n\
    -- initialize default parameters\n\
    local parsed = {}\n\
    for i,v in base.pairs(default or parsed) do parsed[i] = v end\n\
    -- empty url is parsed to nil\n\
    if not url or url == \"\" then return nil, \"invalid url\" end\n\
    -- remove whitespace\n\
    -- url = string.gsub(url, \"%s\", \"\")\n\
    -- get scheme\n\
    url = string.gsub(url, \"^([%w][%w%+%-%.]*)%:\",\n\
        function(s) parsed.scheme = s; return \"\" end)\n\
    -- get authority\n\
    url = string.gsub(url, \"^//([^/]*)\", function(n)\n\
        parsed.authority = n\n\
        return \"\"\n\
    end)\n\
    -- get fragment\n\
    url = string.gsub(url, \"#(.*)$\", function(f)\n\
        parsed.fragment = f\n\
        return \"\"\n\
    end)\n\
    -- get query string\n\
    url = string.gsub(url, \"%?(.*)\", function(q)\n\
        parsed.query = q\n\
        return \"\"\n\
    end)\n\
    -- get params\n\
    url = string.gsub(url, \"%;(.*)\", function(p)\n\
        parsed.params = p\n\
        return \"\"\n\
    end)\n\
    -- path is whatever was left\n\
    if url ~= \"\" then parsed.path = url end\n\
    local authority = parsed.authority\n\
    if not authority then return parsed end\n\
    authority = string.gsub(authority,\"^([^@]*)@\",\n\
        function(u) parsed.userinfo = u; return \"\" end)\n\
    authority = string.gsub(authority, \":([^:%]]*)$\",\n\
        function(p) parsed.port = p; return \"\" end)\n\
    if authority ~= \"\" then \n\
        -- IPv6?\n\
        parsed.host = string.match(authority, \"^%[(.+)%]$\") or authority \n\
    end\n\
    local userinfo = parsed.userinfo\n\
    if not userinfo then return parsed end\n\
    userinfo = string.gsub(userinfo, \":([^:]*)$\",\n\
        function(p) parsed.password = p; return \"\" end)\n\
    parsed.user = userinfo\n\
    return parsed\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Rebuilds a parsed URL from its components.\n\
-- Components are protected if any reserved or unallowed characters are found\n\
-- Input\n\
--   parsed: parsed URL, as returned by parse\n\
-- Returns\n\
--   a stringing with the corresponding URL\n\
-----------------------------------------------------------------------------\n\
function _M.build(parsed)\n\
    --local ppath = _M.parse_path(parsed.path or \"\")\n\
    --local url = _M.build_path(ppath)\n\
    local url = parsed.path or \"\"\n\
    if parsed.params then url = url .. \";\" .. parsed.params end\n\
    if parsed.query then url = url .. \"?\" .. parsed.query end\n\
    local authority = parsed.authority\n\
    if parsed.host then\n\
        authority = parsed.host\n\
        if string.find(authority, \":\") then -- IPv6?\n\
            authority = \"[\" .. authority .. \"]\"\n\
        end\n\
        if parsed.port then authority = authority .. \":\" .. base.tostring(parsed.port) end\n\
        local userinfo = parsed.userinfo\n\
        if parsed.user then\n\
            userinfo = parsed.user\n\
            if parsed.password then\n\
                userinfo = userinfo .. \":\" .. parsed.password\n\
            end\n\
        end\n\
        if userinfo then authority = userinfo .. \"@\" .. authority end\n\
    end\n\
    if authority then url = \"//\" .. authority .. url end\n\
    if parsed.scheme then url = parsed.scheme .. \":\" .. url end\n\
    if parsed.fragment then url = url .. \"#\" .. parsed.fragment end\n\
    -- url = string.gsub(url, \"%s\", \"\")\n\
    return url\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Builds a absolute URL from a base and a relative URL according to RFC 2396\n\
-- Input\n\
--   base_url\n\
--   relative_url\n\
-- Returns\n\
--   corresponding absolute url\n\
-----------------------------------------------------------------------------\n\
function _M.absolute(base_url, relative_url)\n\
    local base_parsed\n\
    if base.type(base_url) == \"table\" then\n\
        base_parsed = base_url\n\
        base_url = _M.build(base_parsed)\n\
    else\n\
        base_parsed = _M.parse(base_url)\n\
    end\n\
    local result\n\
    local relative_parsed = _M.parse(relative_url)\n\
    if not base_parsed then\n\
        result = relative_url\n\
    elseif not relative_parsed then\n\
        result = base_url\n\
    elseif relative_parsed.scheme then\n\
        result = relative_url\n\
    else\n\
        relative_parsed.scheme = base_parsed.scheme\n\
        if not relative_parsed.authority then\n\
            relative_parsed.authority = base_parsed.authority\n\
            if not relative_parsed.path then\n\
                relative_parsed.path = base_parsed.path\n\
                if not relative_parsed.params then\n\
                    relative_parsed.params = base_parsed.params\n\
                    if not relative_parsed.query then\n\
                        relative_parsed.query = base_parsed.query\n\
                    end\n\
                end\n\
            else    \n\
                relative_parsed.path = absolute_path(base_parsed.path or \"\",\n\
                    relative_parsed.path)\n\
            end\n\
        end\n\
        result = _M.build(relative_parsed)\n\
    end\n\
    return remove_dot_components(result)\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Breaks a path into its segments, unescaping the segments\n\
-- Input\n\
--   path\n\
-- Returns\n\
--   segment: a table with one entry per segment\n\
-----------------------------------------------------------------------------\n\
function _M.parse_path(path)\n\
    local parsed = {}\n\
    path = path or \"\"\n\
    --path = string.gsub(path, \"%s\", \"\")\n\
    string.gsub(path, \"([^/]+)\", function (s) table.insert(parsed, s) end)\n\
    for i = 1, #parsed do\n\
        parsed[i] = _M.unescape(parsed[i])\n\
    end\n\
    if string.sub(path, 1, 1) == \"/\" then parsed.is_absolute = 1 end\n\
    if string.sub(path, -1, -1) == \"/\" then parsed.is_directory = 1 end\n\
    return parsed\n\
end\n\
\n\
-----------------------------------------------------------------------------\n\
-- Builds a path component from its segments, escaping protected characters.\n\
-- Input\n\
--   parsed: path segments\n\
--   unsafe: if true, segments are not protected before path is built\n\
-- Returns\n\
--   path: corresponding path stringing\n\
-----------------------------------------------------------------------------\n\
function _M.build_path(parsed, unsafe)\n\
    local path = \"\"\n\
    local n = #parsed\n\
    if unsafe then\n\
        for i = 1, n-1 do\n\
            path = path .. parsed[i]\n\
            path = path .. \"/\"\n\
        end\n\
        if n > 0 then\n\
            path = path .. parsed[n]\n\
            if parsed.is_directory then path = path .. \"/\" end\n\
        end\n\
    else\n\
        for i = 1, n-1 do\n\
            path = path .. protect_segment(parsed[i])\n\
            path = path .. \"/\"\n\
        end\n\
        if n > 0 then\n\
            path = path .. protect_segment(parsed[n])\n\
            if parsed.is_directory then path = path .. \"/\" end\n\
        end\n\
    end\n\
    if parsed.is_absolute then path = \"/\" .. path end\n\
    return path\n\
end\n\
\n\
return _M\n\
";

int luasocket_loadluasrc_ftp(lua_State *L) {
  luaL_dostring(L, luasocket_ftp_luafilesource);
  return 1;
}
int luasocket_loadluasrc_headers(lua_State *L) {
  luaL_dostring(L, luasocket_headers_luafilesource);
  return 1;
}
int luasocket_loadluasrc_http(lua_State *L) {
  luaL_dostring(L, luasocket_http_luafilesource);
  return 1;
}
int luasocket_loadluasrc_ltn12(lua_State *L) {
  luaL_dostring(L, luasocket_ltn12_luafilesource);
  return 1;
}
int luasocket_loadluasrc_mbox(lua_State *L) {
  luaL_dostring(L, luasocket_mbox_luafilesource);
  return 1;
}
int luasocket_loadluasrc_mime(lua_State *L) {
  luaL_dostring(L, luasocket_mime_luafilesource);
  return 1;
}
int luasocket_loadluasrc_smtp(lua_State *L) {
  luaL_dostring(L, luasocket_smtp_luafilesource);
  return 1;
}
int luasocket_loadluasrc_socket(lua_State *L) {
  luaL_dostring(L, luasocket_socket_luafilesource);
  return 1;
}
int luasocket_loadluasrc_tp(lua_State *L) {
  luaL_dostring(L, luasocket_tp_luafilesource);
  return 1;
}
int luasocket_loadluasrc_url(lua_State *L) {
  luaL_dostring(L, luasocket_url_luafilesource);
  return 1;
}

void luasocket_preload_luasrc_definitions(lua_State *L) {
// These definitions come from https://github.com/lunarmodules/luasocket/blob/5b18e475f38fcf28429b1cc4b17baee3b9793a62/rockspec/luasocket-3.0rc2-1.rockspec#L41
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luasocket_loadluasrc_ftp);
  lua_setfield(L, -2, "socket.ftp");
  lua_pushcfunction(L, luasocket_loadluasrc_headers);
  lua_setfield(L, -2, "socket.headers");
  lua_pushcfunction(L, luasocket_loadluasrc_http);
  lua_setfield(L, -2, "socket.http");
  lua_pushcfunction(L, luasocket_loadluasrc_ltn12);
  lua_setfield(L, -2, "ltn12");
  lua_pushcfunction(L, luasocket_loadluasrc_mbox);
  lua_setfield(L, -2, "mbox");
  lua_pushcfunction(L, luasocket_loadluasrc_mime);
  lua_setfield(L, -2, "mime");
  lua_pushcfunction(L, luasocket_loadluasrc_smtp);
  lua_setfield(L, -2, "socket.smtp");
  lua_pushcfunction(L, luasocket_loadluasrc_socket);
  lua_setfield(L, -2, "socket");
  lua_pushcfunction(L, luasocket_loadluasrc_tp);
  lua_setfield(L, -2, "socket.tp");
  lua_pushcfunction(L, luasocket_loadluasrc_url);
  lua_setfield(L, -2, "socket.url");
  lua_pop(L, 2);
}
