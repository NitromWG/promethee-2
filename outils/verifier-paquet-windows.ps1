# Prométhée : vérification bloquante d'un paquet Windows. Licence GPL-3.0-only.
#
#   pwsh outils/verifier-paquet-windows.ps1 -Dossier paquet\Promethee-0.25.0 [-Identifiant 0.25.0+abc]
#
# Pour chaque .exe et .dll du paquet (extensions Qt comprises), lit les bibliothèques importées (dumpbin
# /dependents, chargement différé compris) et exige qu'elles soient dans le dossier du programme, ou qu'elles
# fassent partie de Windows. Les bibliothèques d'exécution Visual C++ ne comptent jamais comme « Windows » :
# la machine de compilation les a, un PC neuf non. Vérifie aussi le contenu obligatoire du paquet et
# l'absence de fichiers de développement ou de bibliothèques de débogage. Code de sortie 1 au moindre défaut.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Dossier,
  [string]$Identifiant = "",
  [string]$DossierOpenMP = ""
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$Dossier = (Resolve-Path $Dossier).Path
$defauts = New-Object System.Collections.Generic.List[string]

function Trouver-Dumpbin {
  $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  $trouve = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" |
    Sort-Object | Select-Object -Last 1
  if (-not $trouve) { throw "dumpbin.exe introuvable" }
  return $trouve
}

function Dependances([string]$module) {
  $sortie = & $script:dumpbin /nologo /dependents $module
  $liste = @()
  $dans = $false
  foreach ($l in $sortie) {
    if ($l -match 'has the following (delay load )?dependencies') { $dans = $true; continue }
    if ($dans -and $l -match '^\s+(\S+\.(dll|DLL|drv|DRV|exe|EXE))\s*$') { $liste += $Matches[1]; continue }
    if ($dans -and $l -match '^\s*Summary') { $dans = $false }
  }
  return $liste | Sort-Object -Unique
}

# Bibliothèques livrées avec Visual Studio, jamais présentes d'office sur un Windows neuf.
$redistribuables = '^(msvcp\d+(_\w+)?|vcruntime\d+(_\w+)?|concrt\d+|vccorlib\d+|vcomp\d+|vcamp\d+|mfc\d+\w*)$'
# Bibliothèques de débogage : un paquet publié ne doit jamais en dépendre.
$debogage = '^(msvcp\d+d|vcruntime\d+(_1)?d|ucrtbased|concrt\d+d|vccorlib\d+d|vcomp\d+d|qt6cored|qt6guid|qt6widgetsd|qt6openglwidgetsd|qt6opengld)$'
# Bibliothèques de Windows connues (en plus de l'ensemble des API « api-ms-win-* » et « ext-ms-* »).
$windowsConnues = @("kernel32", "user32", "gdi32", "advapi32", "shell32", "ole32", "oleaut32", "comdlg32", "comctl32", "ws2_32", "winmm", "imm32",
  "version", "dwmapi", "uxtheme", "shlwapi", "setupapi", "crypt32", "bcrypt", "ncrypt", "secur32", "netapi32", "userenv", "wtsapi32", "d3d9", "d3d11",
  "d3d12", "dxgi", "dcomp", "dwrite", "d2d1", "opengl32", "glu32", "mpr", "iphlpapi", "dnsapi", "authz", "powrprof", "ntdll", "rpcrt4", "cfgmgr32",
  "ucrtbase", "msvcrt", "winspool", "hid", "dbghelp", "psapi", "shcore", "dxcore", "d3dcompiler_47", "wininet", "winhttp", "propsys", "msimg32",
  "usp10", "oleacc", "uiautomationcore", "wsock32", "normaliz", "credui", "cryptui", "wintrust", "msi", "dinput8", "xinput1_4", "mf", "mfplat",
  "mfreadwrite", "wldap32", "netutils", "srvcli", "winsta", "sspicli", "kernelbase", "gdiplus", "windowscodecs", "comppkgsup", "dxva2", "evr")
$system32 = Join-Path $env:SystemRoot "System32"
$systeme = @{}
Get-ChildItem $system32 -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in ".dll", ".drv" } | ForEach-Object { $systeme[$_.Name.ToLower()] = $true }
$presents = @{}
Get-ChildItem $Dossier -File | ForEach-Object { $presents[$_.Name.ToLower()] = $true }

$script:dumpbin = Trouver-Dumpbin
$modules = Get-ChildItem $Dossier -Recurse -File -Include *.exe, *.dll
$aRevoir = @{}
foreach ($m in $modules) {
  $rel = $m.FullName.Substring($Dossier.Length + 1)
  foreach ($d in Dependances $m.FullName) {
    $nom = $d.ToLower()
    $base = [IO.Path]::GetFileNameWithoutExtension($nom)
    if ($base -match $debogage) { $defauts.Add("$rel dépend d'une bibliothèque de débogage : $d"); continue }
    if ($presents.ContainsKey($nom)) { continue }
    if ($nom -like "api-ms-win-*" -or $nom -like "ext-ms-*") { continue }
    if ($base -match $redistribuables) {
      if ($nom -like "vcomp*" -and $DossierOpenMP -and (Test-Path (Join-Path $DossierOpenMP $d))) {
        Copy-Item (Join-Path $DossierOpenMP $d) $Dossier
        $presents[$nom] = $true
        Write-Host "runtime OpenMP ajouté : $d"
        continue
      }
      $defauts.Add("$rel importe $d, bibliothèque d'exécution Visual C++ absente du paquet")
      continue
    }
    if ($systeme.ContainsKey($nom)) {
      if ($windowsConnues -notcontains $base) { $aRevoir[$nom] = $rel }
      continue
    }
    $defauts.Add("$rel importe $d, introuvable dans le paquet comme dans Windows")
  }
}
foreach ($k in $aRevoir.Keys) { Write-Host "::warning::$k (importée par $($aRevoir[$k])) est prise dans System32 de la machine de compilation : vérifier qu'elle fait partie de Windows." }

# Contenu obligatoire
foreach ($f in "Promethee.exe", "platforms\qwindows.dll", "LICENSE", "TIERS.md", "LISEZMOI.txt", "SOURCE.txt", "VERSIONS.txt", "sbom\promethee.spdx.json",
  "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
  if (-not (Test-Path (Join-Path $Dossier $f))) { $defauts.Add("fichier obligatoire absent : $f") }
}
if (-not (Get-ChildItem (Join-Path $Dossier "source") -Filter "*-source.zip" -ErrorAction SilentlyContinue)) { $defauts.Add("source correspondante absente (source\*-source.zip)") }
Get-ChildItem $Dossier -Recurse -File -Include *.lib, *.exp, *.pdb, *.ilk, *.obj | ForEach-Object { $defauts.Add("fichier de développement dans le paquet : $($_.Name)") }
# Prométhée ne se connecte à aucun réseau : Qt6Network.dll ne sert qu'au canal local (tube nommé), et ni extension
# TLS ni extension TUIO ne doit l'accompagner
if (-not (Test-Path (Join-Path $Dossier "Qt6Network.dll"))) { $defauts.Add("Qt6Network.dll absente (canal local de l'interface de commandes)") }
if (Test-Path (Join-Path $Dossier "generic\qtuiotouchplugin.dll")) { $defauts.Add("extension tactile TUIO (réseau) dans le paquet : generic\qtuiotouchplugin.dll") }
foreach ($d in "tls", "networkinformation", "bearer") { if (Test-Path (Join-Path $Dossier $d)) { $defauts.Add("extensions réseau inutiles dans le paquet : $d\") } }

# Version inscrite dans l'exécutable (propriétés du fichier) : celle du build.
$infos = (Get-Item (Join-Path $Dossier "Promethee.exe")).VersionInfo
if (-not $infos.ProductVersion) { $defauts.Add("Promethee.exe sans ressource de version") }
elseif ($Identifiant -and $infos.ProductVersion -ne $Identifiant) { $defauts.Add("version du programme ($($infos.ProductVersion)) différente du build ($Identifiant)") }
else { Write-Host "Promethee.exe : version $($infos.FileVersion), produit $($infos.ProductVersion)" }

try {
  $signature = Get-AuthenticodeSignature (Join-Path $Dossier "Promethee.exe")
  Write-Host "Signature Authenticode : $($signature.Status)"
} catch { Write-Host "Signature Authenticode : non vérifiable ici ($_)" }

if ($defauts.Count -gt 0) {
  foreach ($d in $defauts) { Write-Host "::error::$d" }
  Write-Host "$($defauts.Count) défaut(s) : paquet refusé."
  exit 1
}
Write-Host "Paquet conforme : $($modules.Count) modules vérifiés, toutes leurs dépendances sont livrées ou font partie de Windows."
exit 0
