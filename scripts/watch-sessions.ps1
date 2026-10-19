#!/usr/bin/env pwsh
# watch-sessions.ps1 - dispatcher tooling: one process that waits for other agent
# sessions and for PRs to reach a state the dispatcher must act on, then prints
# one summary line per item. PowerShell port of scripts/watch-sessions.sh; same
# semantics. See that file for the authoritative behaviour description.
#
# Usage:
#   scripts/watch-sessions.ps1 [options] <ses_... | PR#> [<ses_... | PR#> ...]
#
# Options:
#   --timeout SEC    per-item cap (default 1800)
#   --interval SEC   PR poll interval (default 15)
#   --grace SEC      session idle-at-start grace (default 20)
#   --base URL       API base (default: discover the listening opencode server)
#   -h, --help       this text
#
# NOTE (dispatcher usage): this process exits only when EVERY item has settled,
# and the dispatcher's wake-up is tied to this command finishing - so one
# watcher over several items delays every wake to the slowest one. Launch ONE
# watcher per item so each completion wakes you independently; use a multi-item
# invocation only when you genuinely want the whole set to settle before acting.
#
# Sessions: blocks on POST /api/experimental/session/<id>/wait (204 when the
# session goes idle), then confirms a message landed after the watch began. A
# session idle at watch start with no new activity is reported as such after the
# grace period. A `user` message arriving must NOT count as completion (it is the
# dispatcher's own prompt) - only agent-side records end a turn.
#
# PRs: polls gh until MERGED/CLOSED, a conflict with main (mergeStateStatus
# DIRTY), a fresh VERDICT comment on the CURRENT head (NEEDS CHANGE, or
# MERGEABLE without auto-merge armed), or a failing check. MERGEABLE with
# auto-merge armed keeps waiting.
#
# Read-only: API GETs, the wait call, and gh reads. No repo writes, no merges.
#
# Windows notes:
#   * Auth comes from an opencode service.json. Two exist on this host: the
#     desktop/remote one at ~/.config/opencode/service.json and the LIVE local
#     server's at ~/.local/state/opencode/service.json. The password is read at
#     runtime, turned into an in-memory Authorization header, and never written
#     to disk or passed on a command line. OPENCODE_SERVICE overrides the file.
#   * Port discovery: Get-NetTCPConnection filtered to the opencode process,
#     with the local service.json's `url` as a fallback; --base / OPENCODE_BASE
#     override both.
#   * gh needs git on PATH; this script prepends the known Git for Windows dir.
#   * A `#N` item may be parsed as a PowerShell comment in some hosts - quote it
#     ('#1') or use the bare number.
#   * Running a .ps1 may be policy-blocked; invoke with `-ExecutionPolicy Bypass`.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# --- options + items: parsed by hand so `--flag` works (PowerShell has no --) --
$Timeout = 1800
$Interval = 15
$Grace = 20
$Base = ''
$Child = $false
$Items = New-Object System.Collections.Generic.List[string]

function Show-Help {
  Get-Content -LiteralPath $PSCommandPath | Select-Object -Skip 1 |
    Where-Object { $_ -match '^#' } |
    ForEach-Object { $_ -replace '^#\s?', '' }
}

$VerdictSelftest = $false
$i = 0
while ($i -lt $args.Count) {
  $a = [string]$args[$i]
  switch ($a) {
    '--timeout'  { $i++; if ($i -ge $args.Count) { break }; $Timeout  = [int]$args[$i] }
    '--interval' { $i++; if ($i -ge $args.Count) { break }; $Interval = [int]$args[$i] }
    '--grace'    { $i++; if ($i -ge $args.Count) { break }; $Grace    = [int]$args[$i] }
    '--base'     { $i++; if ($i -ge $args.Count) { break }; $Base     = [string]$args[$i] }
    '--child'    { $Child = $true }
    '--verdict-selftest' { $VerdictSelftest = $true }
    '-h'         { Show-Help; exit 0 }
    '--help'     { Show-Help; exit 0 }
    default {
      if ($a -like '--*') { [Console]::Error.WriteLine("watch-sessions: unknown option: $a"); exit 2 }
      $Items.Add($a)
    }
  }
  $i++
}

# --- gh / git: gh needs git on PATH, so set it here rather than assume it -----
$gitBin = if ($env:GIT_BIN) { $env:GIT_BIN } else { 'C:\Users\Bruce\AppData\Local\GitHubDesktop\app-3.6.7\resources\app\git\cmd\git.exe' }
$gitDir = Split-Path -Parent $gitBin
if (Test-Path -LiteralPath $gitDir) { $env:PATH = "$gitDir;$env:PATH" }
$GhBin = if ($env:GH_BIN) { $env:GH_BIN } else { 'C:\Users\Bruce\tools\gh\gh.exe' }

# --- repo from git, not from gh's inference -----------------------------------
# This clone has two remotes: origin = the fork, parent = the parent repo (renamed
# from `upstream` so gh no longer treats it as a fork base and mis-targets it).
# Derive owner/name from `git remote get-url origin` (as scripts/verify-pr.py
# does) and make every gh call explicit via GH_REPO. Do NOT rely on gh inference.
function Get-RepoFromRemote {
  try { $url = (& $script:gitBin remote get-url origin 2>$null) -join '' } catch { return '' }
  if ($url -match 'github\.com[:/]+([^/]+/[^/]+?)(?:\.git)?$') { return $Matches[1] }
  return ''
}
$Repo = Get-RepoFromRemote
if (-not $Repo) {
  [Console]::Error.WriteLine('watch-sessions: cannot resolve the fork repo from `git remote get-url origin`; set GH_REPO explicitly')
  exit 2
}
$env:GH_REPO = $Repo

# --- auth: read at runtime, never echoed, never on a command line -------------
$serviceCandidates = New-Object System.Collections.Generic.List[string]
if ($env:OPENCODE_SERVICE) { $serviceCandidates.Add($env:OPENCODE_SERVICE) }
$serviceCandidates.Add((Join-Path $HOME '.config\opencode\service.json'))
$serviceCandidates.Add((Join-Path $HOME '.local\state\opencode\service.json'))

function New-AuthHeader([string]$secret) {
  $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("opencode:$secret"))
  return @{ Authorization = "Basic $b64" }
}

# --- base URL candidates: --base / env, else netstat, else the state url ------
function Get-BaseCandidates([string]$explicit) {
  $out = New-Object System.Collections.Generic.List[string]
  if ($explicit) { $out.Add($explicit); return $out }
  if ($env:OPENCODE_BASE) { $out.Add($env:OPENCODE_BASE); return $out }
  try {
    Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | ForEach-Object {
      $p = Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue
      if ($p -and $p.ProcessName -like 'opencode*') { $out.Add("http://127.0.0.1:$($_.LocalPort)") }
    }
  } catch { }
  $stateSvc = Join-Path $HOME '.local\state\opencode\service.json'
  if (Test-Path -LiteralPath $stateSvc) {
    try { $u = (Get-Content -Raw -LiteralPath $stateSvc | ConvertFrom-Json).url; if ($u) { $out.Add([string]$u) } } catch { }
  }
  return ($out | Select-Object -Unique)
}

function Resolve-Endpoint([string]$explicitBase) {
  # Returns @{ Base=...; Header=... } for the first (base, password) pair that
  # authenticates (GET /api/info -> 200), or $null. Secrets stay in memory.
  foreach ($b in Get-BaseCandidates $explicitBase) {
    foreach ($svc in $serviceCandidates) {
      if (-not (Test-Path -LiteralPath $svc)) { continue }
      $secret = $null
      try { $secret = (Get-Content -Raw -LiteralPath $svc | ConvertFrom-Json).password } catch { continue }
      if ([string]::IsNullOrEmpty($secret)) { continue }
      $hdr = New-AuthHeader $secret
      try {
        Invoke-WebRequest -Uri "$b/api/info" -Headers $hdr -TimeoutSec 5 -UseBasicParsing | Out-Null
        return @{ Base = $b; Header = $hdr }
      } catch { }
    }
  }
  return $null
}

$endpoint = Resolve-Endpoint $Base
if ($null -eq $endpoint) {
  [Console]::Error.WriteLine('watch-sessions: no authenticating opencode server found (pass --base / OPENCODE_BASE, or OPENCODE_SERVICE)')
  exit 2
}
$script:ApiBase = $endpoint.Base
$script:AuthHeader = $endpoint.Header

# --- API helpers --------------------------------------------------------------
function Invoke-Api([string]$Path, [int]$TimeoutSec = 20) {
  try {
    $r = Invoke-WebRequest -Uri "$($script:ApiBase)$Path" -Headers $script:AuthHeader -TimeoutSec $TimeoutSec -UseBasicParsing
    return ($r.Content | ConvertFrom-Json)
  } catch { return $null }
}

function Get-SessionLine([string]$Id) {
  $meta = Invoke-Api "/api/session/$Id"
  $out = if ($meta -and $meta.data.outcome) { $meta.data.outcome } else { 'unknown' }
  $text = ''
  $msgs = Invoke-Api "/api/session/$Id/message?limit=10"
  if ($msgs) {
    $assist = @($msgs.data | Where-Object { $_.type -eq 'assistant' })
    if ($assist.Count -gt 0) {
      $last = $assist | Sort-Object { $_.time.created } | Select-Object -Last 1
      $text = (@($last.content | Where-Object { $_.type -eq 'text' } | ForEach-Object { $_.text }) -join ' ')
    }
  }
  $text = ($text -replace '\s+', ' ').Trim()
  if ($text.Length -gt 160) { $text = $text.Substring(0, 160) }
  return "outcome=$out - `"$text`""
}

function Invoke-Gh([string[]]$GhArgs) {
  try { $out = & $script:GhBin @GhArgs 2>$null } catch { return @{ Code = 1; Text = '' } }
  return @{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
}

# --- verdict decision ---------------------------------------------------------
# Decide the verdict state from a PR's issue comments. Returns:
#   @{ Kind='match'; Verdict=...; Sha=... }   a verdict naming the current head
#   @{ Kind='stale'; Verdict=...; Sha=... }   a verdict naming a different head
#   @{ Kind='none' }                          no verdict comment at all
#   @{ Kind='error'; Detail=... }             the lookup could not be read/parsed
#
# Parses JSON with ConvertFrom-Json, NEVER by passing a `--jq` filter with
# embedded double quotes through a native call: PowerShell 5.1 strips those
# quotes, gh receives invalid jq, and the failure was indistinguishable from "no
# verdict". 'error' is a distinct outcome so a broken lookup can never read as an
# empty result.
function Get-VerdictDecision([string]$CommentsJson, [string]$Head) {
  $comments = $null
  try { $comments = $CommentsJson | ConvertFrom-Json } catch { return @{ Kind = 'error'; Detail = 'comment JSON did not parse' } }
  $verdicts = @($comments | Where-Object { $_ -and $_.body -and ([string]$_.body).StartsWith('VERDICT:') })
  if ($verdicts.Count -eq 0) { return @{ Kind = 'none' } }
  $first = (([string]$verdicts[-1].body) -split "`n")[0].Trim()
  $m = [regex]::Match($first, '^VERDICT:\s*(MERGEABLE|NEEDS CHANGE)\s*@\s*([0-9a-f]{40})')
  if (-not $m.Success) { return @{ Kind = 'error'; Detail = "last verdict comment is not a well-formed anchored line: $first" } }
  $verdict = $m.Groups[1].Value.Trim()
  $sha = $m.Groups[2].Value
  if ($sha -ne $Head) { return @{ Kind = 'stale'; Verdict = $verdict; Sha = $sha } }
  return @{ Kind = 'match'; Verdict = $verdict; Sha = $sha }
}

function Invoke-VerdictSelftest {
  $HEAD = 'a' * 40
  $STALE = 'b' * 40
  $cases = @(
    @{ name = 'matching NEEDS CHANGE -> match';        json = "[{`"body`":`"VERDICT: NEEDS CHANGE @ $HEAD`"}]";             head = $HEAD;  want = 'match' },
    @{ name = 'matching MERGEABLE -> match';           json = "[{`"body`":`"VERDICT: MERGEABLE @ $HEAD`"}]";                 head = $HEAD;  want = 'match' },
    @{ name = 'stale verdict -> stale';                json = "[{`"body`":`"VERDICT: MERGEABLE @ $STALE`"}]";                head = $HEAD;  want = 'stale' },
    @{ name = 'no comments -> none';                   json = '[]';                                                       head = $HEAD;  want = 'none' },
    @{ name = 'comments without a verdict -> none';    json = '[{"body":"just a review"}]';                              head = $HEAD;  want = 'none' },
    @{ name = 'malformed JSON -> error (loud)';        json = 'not json';                                                 head = $HEAD;  want = 'error' },
    @{ name = 'starts with VERDICT: but malformed -> error'; json = '[{"body":"VERDICT: NEEDS CHANGE @ not-a-sha"}]'; head = $HEAD; want = 'error' },
    @{ name = 'last verdict wins (earlier matches) -> match'; json = "[{`"body`":`"VERDICT: NEEDS CHANGE @ $STALE`"},{`"body`":`"VERDICT: MERGEABLE @ $HEAD`"}]"; head = $HEAD; want = 'match' }
  )
  $fail = 0
  foreach ($c in $cases) {
    $got = Get-VerdictDecision $c.json $c.head
    $ok = ($got.Kind -eq $c.want)
    if (-not $ok) { $fail++ }
    [Console]::Out.WriteLine(("{0}  {1}  want={2} got={3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $c.name, $c.want, $got.Kind))
  }
  [Console]::Out.WriteLine("verdict selftest: $($cases.Count) cases, $(if ($fail) { 'FAIL' } else { 'ok' })")
  return $(if ($fail) { 1 } else { 0 })
}

# --- session watcher ----------------------------------------------------------
function Watch-Session([string]$Id) {
  $deadline = (Get-Date).AddSeconds($Timeout)
  $baseline = [int64]0
  $b = Invoke-Api "/api/session/$Id/message?limit=1"
  if ($b) { $arr = @($b.data); if ($arr.Count -gt 0 -and $arr[-1].time.created) { $baseline = [int64]$arr[-1].time.created } }
  $idleTicks = 0
  while ($true) {
    if ((Get-Date) -ge $deadline) {
      [Console]::Out.WriteLine("[session] ${Id}: TIMEOUT - $(Get-SessionLine $Id)")
      return 1
    }
    $active = Invoke-Api '/api/session/active'
    $names = @()
    if ($active) { $names = @($active.data.PSObject.Properties.Name) }
    if ($names -contains $Id) {
      $idleTicks = 0
      $remain = [int][Math]::Max(5, ($deadline - (Get-Date)).TotalSeconds)
      try {
        Invoke-WebRequest -Uri "$($script:ApiBase)/api/experimental/session/$Id/wait" -Method Post `
          -Headers $script:AuthHeader -TimeoutSec $remain -UseBasicParsing | Out-Null
      } catch { }
      continue
    }
    $resp = Invoke-Api "/api/session/$Id/message?limit=1"
    $arr = @()
    if ($resp) { $arr = @($resp.data) }
    $lastTime = [int64]0
    $lastType = '?'
    if ($arr.Count -gt 0) {
      if ($arr[-1].time.created) { $lastTime = [int64]$arr[-1].time.created }
      if ($arr[-1].type) { $lastType = [string]$arr[-1].type }
    }
    # A `user` message is the dispatcher's own prompt arriving; it must not count
    # as the agent completing. Only agent-side records end a turn.
    if ($lastTime -gt $baseline -and $lastType -ne 'user') {
      [Console]::Out.WriteLine("[session] ${Id}: done (new $lastType message at $lastTime) - $(Get-SessionLine $Id)")
      return 0
    }
    $idleTicks++
    if (($idleTicks * 2) -ge $Grace) {
      [Console]::Out.WriteLine("[session] ${Id}: idle at watch start, no new activity (${Grace}s) - $(Get-SessionLine $Id)")
      return 0
    }
    Start-Sleep -Seconds 2
  }
}

# --- PR watcher ---------------------------------------------------------------
function Watch-PR([string]$N) {
  $deadline = (Get-Date).AddSeconds($Timeout)
  $repo = $script:Repo
  $staleLogged = $false
  while ($true) {
    $v = Invoke-Gh @('pr', 'view', $N, '--repo', $repo, '--json', 'state,mergeStateStatus,headRefOid,autoMergeRequest,url')
    if ($v.Code -ne 0 -or [string]::IsNullOrWhiteSpace($v.Text)) {
      [Console]::Out.WriteLine("[pr] #${N}: not found or not readable")
      return 1
    }
    $json = $null
    try { $json = $v.Text | ConvertFrom-Json } catch { }
    if ($null -eq $json) { [Console]::Out.WriteLine("[pr] #${N}: not found or not readable"); return 1 }
    $state = [string]$json.state
    $mss = if ($json.mergeStateStatus) { [string]$json.mergeStateStatus } else { '?' }
    $head = if ($json.headRefOid) { [string]$json.headRefOid } else { '' }
    $armed = ($null -ne $json.autoMergeRequest)
    if ($state -eq 'MERGED' -or $state -eq 'CLOSED') { [Console]::Out.WriteLine("[pr] #${N}: $state"); return 0 }
    if ($mss -eq 'DIRTY') { [Console]::Out.WriteLine("[pr] #${N}: CONFLICTING with main - needs a rebase"); return 0 }
    if ($repo) {
      $cq = Invoke-Gh @('api', "repos/$repo/issues/$N/comments", '--paginate')
      if ($cq.Code -ne 0) {
        [Console]::Out.WriteLine("[pr] #${N}: VERDICT LOOKUP FAILED (gh exit $($cq.Code)) - cannot tell whether a verdict exists")
        return 1
      }
      $vd = Get-VerdictDecision $cq.Text $head
      switch ($vd.Kind) {
        'error' {
          [Console]::Out.WriteLine("[pr] #${N}: VERDICT LOOKUP FAILED - $($vd.Detail)")
          return 1
        }
        'stale' {
          if (-not $staleLogged) {
            [Console]::Out.WriteLine("[pr] #${N}: verdict names $($vd.Sha) but head is $head - stale, ignoring")
            $staleLogged = $true
          }
        }
        'match' {
          if ($vd.Verdict -eq 'NEEDS CHANGE') {
            [Console]::Out.WriteLine("[pr] #${N}: NEEDS CHANGE @ $($head.Substring(0, [Math]::Min(7, $head.Length))) - needs a fix")
            return 0
          }
          if ($vd.Verdict -eq 'MERGEABLE' -and -not $armed) {
            [Console]::Out.WriteLine("[pr] #${N}: MERGEABLE @ $($head.Substring(0, [Math]::Min(7, $head.Length))) but auto-merge is NOT armed - needs your action")
            return 0
          }
        }
      }
    }
    # checks: parse JSON in PowerShell, never a jq filter through a native call. A
    # lookup that errors is REPORTED, not silently read as "no failing checks".
    $chk = Invoke-Gh @('pr', 'checks', $N, '--repo', $repo, '--json', 'state')
    $fails = 0
    if ($chk.Code -ne 0) {
      if ($chk.Text -match 'no checks reported') { $fails = 0 }
      else { [Console]::Out.WriteLine("[pr] #${N}: CHECKS LOOKUP FAILED (gh exit $($chk.Code)) - cannot tell whether a check is failing") }
    } elseif (-not [string]::IsNullOrWhiteSpace($chk.Text)) {
      try { $fails = @(($chk.Text | ConvertFrom-Json) | Where-Object { $_.state -eq 'FAILURE' }).Count }
      catch { [Console]::Out.WriteLine("[pr] #${N}: CHECKS LOOKUP FAILED - checks JSON did not parse"); $fails = 0 }
    }
    if ($fails -gt 0) { [Console]::Out.WriteLine("[pr] #${N}: $fails check(s) failing"); return 0 }
    if ((Get-Date) -ge $deadline) { [Console]::Out.WriteLine("[pr] #${N}: TIMEOUT (state=$state)"); return 1 }
    Start-Sleep -Seconds $Interval
  }
}

# --- run ----------------------------------------------------------------------
function Invoke-Item-Waiter([string]$Item) {
  if ($Item -like 'ses_*') { return (Watch-Session $Item) }
  if ($Item -like '#*') { return (Watch-PR $Item.Substring(1)) }
  if ($Item -match '^\d+$') { return (Watch-PR $Item) }
  [Console]::Error.WriteLine("watch-sessions: skipping unrecognized item: $Item")
  return 3
}

if ($VerdictSelftest) { exit (Invoke-VerdictSelftest) }

if ($Items.Count -eq 0) {
  [Console]::Error.WriteLine('usage: watch-sessions.ps1 [--timeout S] [--interval S] [--grace S] [--base URL] <ses_... | PR#> ...')
  exit 2
}

if ($Child) {
  # One item, one waiter, one line of output. The parent captures stdout.
  if ($Items.Count -ne 1) { [Console]::Error.WriteLine('watch-sessions: --child expects exactly one item'); exit 2 }
  $rc = Invoke-Item-Waiter $Items[0]
  if ($rc -eq 3) { exit 0 }
  exit $rc
}

# Parent: one backgrounded waiter per item, each writing its own file, then print
# one line per item in order (mirrors the bash subshell-per-item + temp files).
$stamp = { (Get-Date).ToString('HH:mm:ss') }
Write-Output "== watch-sessions: $($Items.Count) item(s) - $(& $stamp) - $script:ApiBase"

$work = Join-Path ([IO.Path]::GetTempPath()) ("watch-sessions-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$psExe = (Get-Process -Id $PID).Path
$procs = @()
$files = @()
$n = 0
foreach ($it in $Items) {
  $recognized = ($it -like 'ses_*') -or ($it -like '#*') -or ($it -match '^\d+$')
  if (-not $recognized) { [Console]::Error.WriteLine("watch-sessions: skipping unrecognized item: $it"); continue }
  $n++
  $f = Join-Path $work ('{0:d2}.out' -f $n)
  $e = Join-Path $work ('{0:d2}.err' -f $n)
  $files += $f
  $cargs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath,
             '--child', '--timeout', "$Timeout", '--interval', "$Interval", '--grace', "$Grace",
             '--base', $script:ApiBase, $it)
  $procs += Start-Process -FilePath $psExe -ArgumentList $cargs -NoNewWindow -PassThru `
    -RedirectStandardOutput $f -RedirectStandardError $e
}
if ($procs.Count -gt 0) { $procs | Wait-Process }

$rc = 0
foreach ($f in $files) {
  if (Test-Path -LiteralPath $f) {
    $line = Get-Content -Raw -LiteralPath $f
    if ([string]::IsNullOrWhiteSpace($line)) {
      $e = [IO.Path]::ChangeExtension($f, '.err')
      $line = if (Test-Path -LiteralPath $e) { Get-Content -Raw -LiteralPath $e } else { '' }
    }
    if ($null -eq $line) { $line = '' }
    Write-Output ($line.TrimEnd("`r", "`n"))
    if ($line -match 'TIMEOUT|not found|FAILED') { $rc = 1 }
  }
}
Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
Write-Output "== watch-sessions: settled - $(& $stamp)"
exit $rc
