# Prométhée : construit le paquet Windows prêt à lancer, puis le vérifie. Licence GPL-3.0-only.
#
#   pwsh outils/paquet-windows.ps1 -Build build -Sortie paquet -Vcpkg <vcpkg.exe> -VcpkgInstalle vcpkg_installed\x64-windows
#
# Contenu du paquet : Promethee.exe et ses bibliothèques (Qt, OpenCascade et leurs dépendances), les
# bibliothèques d'exécution Visual C++ (déploiement local autorisé par Microsoft, voir LISEZMOI.txt), les
# licences, la source correspondante (archive git du commit compilé), l'inventaire des composants (SBOM SPDX),
# les versions exactes et les sommes SHA-256 de chaque fichier. Le script échoue au moindre manque : une
# bibliothèque importée absente, une bibliothèque de débogage, un fichier de développement (.lib, .pdb…).
[CmdletBinding()]
param(
  [string]$Build = "build",
  [string]$Sortie = "paquet",
  [string]$Vcpkg = "",
  [string]$VcpkgInstalle = "vcpkg_installed\x64-windows",
  [string]$QtDir = $env:QT_ROOT_DIR
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$racine = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:dossierOpenMP = ""

function Lire-Version {
  $infos = @{}
  foreach ($l in Get-Content (Join-Path $Build "genere\version.txt")) {
    if ($l -match '^([a-z]+)=(.*)$') { $infos[$Matches[1]] = $Matches[2] }
  }
  return $infos
}

function Trouver-VisualStudio {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw "vswhere.exe introuvable : Visual Studio n'est pas installé." }
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
  if (-not $vs) { throw "Aucune installation de Visual Studio avec les outils C++ x64." }
  return $vs
}

# Dossier des bibliothèques d'exécution redistribuables qui correspondent au compilateur utilisé.
function Trouver-RuntimeVC([string]$vs) {
  $candidats = @()
  if ($env:VCToolsRedistDir) { $candidats += $env:VCToolsRedistDir }
  $defaut = Join-Path $vs "VC\Auxiliary\Build\Microsoft.VCRedistVersion.default.txt"
  if (Test-Path $defaut) { $candidats += (Join-Path $vs ("VC\Redist\MSVC\" + (Get-Content $defaut -TotalCount 1).Trim())) }
  $candidats += Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | ForEach-Object { $_.FullName }
  foreach ($c in $candidats) {
    $crt = Get-ChildItem (Join-Path $c "x64") -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($crt -and (Test-Path (Join-Path $crt.FullName "vcruntime140.dll"))) { return $crt.FullName }
  }
  throw "Bibliothèques d'exécution Visual C++ (x64\Microsoft.VC*.CRT) introuvables dans $vs\VC\Redist\MSVC."
}

$v = Lire-Version
$version = $v["version"]
$identifiant = $v["identifiant"]
$nomPaquet = "Promethee-$version"
$dest = Join-Path $Sortie $nomPaquet
Write-Host "Paquet $nomPaquet ($identifiant)"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

# 1. Programme et bibliothèques déposées par vcpkg à côté de lui, sans fichiers de développement.
$binaires = Join-Path $Build "app\Release"
Get-ChildItem $binaires -File | Where-Object { $_.Extension -notin ".lib", ".exp", ".pdb", ".ilk", ".manifest" } |
  ForEach-Object { Copy-Item $_.FullName $dest }
$exe = Join-Path $dest "Promethee.exe"
if (-not (Test-Path $exe)) { throw "Promethee.exe absent de $binaires" }

# 2. Signature Authenticode, si un certificat est fourni (secrets PROMETHEE_CERTIFICAT_PFX en base64 et
#    PROMETHEE_CERTIFICAT_MDP). Avant le calcul des sommes : la signature modifie le fichier.
if ($env:PROMETHEE_CERTIFICAT_PFX) {
  $signtool = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\*\x64\signtool.exe") | Sort-Object FullName | Select-Object -Last 1
  $pfx = Join-Path $env:RUNNER_TEMP "promethee.pfx"
  [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:PROMETHEE_CERTIFICAT_PFX))
  & $signtool.FullName sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /f $pfx /p $env:PROMETHEE_CERTIFICAT_MDP $exe
  $code = $LASTEXITCODE
  Remove-Item $pfx -Force
  if ($code -ne 0) { throw "Signature de Promethee.exe impossible" }
  Write-Host "Promethee.exe signé."
} else {
  Write-Host "::notice::Paquet non signé (aucun certificat de signature de code configuré)."
}

# 3. Qt : bibliothèques, extensions (plateforme Windows, styles, formats d'image) et traductions françaises.
$windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
& $windeployqt --release --no-compiler-runtime --translations fr $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt a échoué" }
# Prométhée n'ouvre aucun port réseau. Qt6Network.dll reste dans le paquet : elle porte le canal local (tube nommé
# réservé à l'utilisateur) par lequel un programme extérieur pilote l'atelier (interface JSON-RPC, lot L13).
# Les extensions réseau que windeployqt ajoute parfois (TLS, état du réseau, porteuses) sont retirées.
foreach ($d in "tls", "networkinformation", "bearer") {
  $p = Join-Path $dest $d
  if (Test-Path $p) { Remove-Item -Recurse -Force $p; Write-Host "retiré : $d\ (réseau inutilisé)" }
}
# Même chose pour l'extension tactile TUIO (generic\qtuiotouchplugin.dll), qui reçoit ses événements par le réseau :
# le windeployqt de Qt 6.8 l'ajoute ; Prométhée ne s'en sert pas.
$tuio = Join-Path $dest "generic\qtuiotouchplugin.dll"
if (Test-Path $tuio) { Remove-Item -Force $tuio; Write-Host "retiré : generic\qtuiotouchplugin.dll (TUIO, réseau)" }
$generic = Join-Path $dest "generic"
if ((Test-Path $generic) -and -not (Get-ChildItem $generic)) { Remove-Item -Force $generic }

# 4. Bibliothèques d'exécution Visual C++ du compilateur utilisé (msvcp140, vcruntime140, vcruntime140_1…).
$vs = Trouver-VisualStudio
$crt = Trouver-RuntimeVC $vs
Get-ChildItem $crt -Filter *.dll | ForEach-Object { Copy-Item $_.FullName $dest; Write-Host "runtime : $($_.Name)" }
$openmp = Join-Path (Split-Path $crt) ((Split-Path $crt -Leaf) -replace 'CRT$', 'OpenMP')
if (Test-Path $openmp) { $script:dossierOpenMP = $openmp }

# 5. Documents : lisez-moi, licences, composants tiers, source correspondante.
Copy-Item (Join-Path $racine "LICENSE"), (Join-Path $racine "TIERS.md") $dest
$remplacer = {
  param($texte)
  $texte.Replace("@VERSION@", $version).Replace("@IDENTIFIANT@", $identifiant).Replace("@REVISION@", $v["revision"]).Replace("@DATE@", $v["date"])
}
foreach ($modele in "LISEZMOI.txt", "SOURCE.txt") {
  $texte = & $remplacer (Get-Content -Raw -Encoding utf8 (Join-Path $racine "outils\paquet\$modele.in"))
  [IO.File]::WriteAllText((Join-Path $dest $modele), $texte.Replace("`r`n", "`n").Replace("`n", "`r`n"), [Text.UTF8Encoding]::new($true))
}
$licences = Join-Path $dest "licences"
New-Item -ItemType Directory -Force -Path $licences | Out-Null
Copy-Item (Join-Path $racine "licences\*") $licences
if (Test-Path (Join-Path $VcpkgInstalle "share")) {
  Get-ChildItem (Join-Path $VcpkgInstalle "share") -Directory | ForEach-Object {
    $c = Join-Path $_.FullName "copyright"
    if (Test-Path $c) { Copy-Item $c (Join-Path $licences "$($_.Name).txt") }
  }
}
# Source correspondante : l'archive exacte du commit compilé (GPL-3.0, article 6).
$source = Join-Path $dest "source"
New-Item -ItemType Directory -Force -Path $source | Out-Null
git -C $racine archive --format=zip --prefix="promethee-$version/" -o (Join-Path $source "promethee-$version-source.zip") HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive a échoué : la source correspondante manque" }

# 6. Versions exactes et inventaire SPDX (SBOM).
$sbom = Join-Path $dest "sbom"
New-Item -ItemType Directory -Force -Path $sbom | Out-Null
$paquetsVcpkg = @()
if ($Vcpkg -and (Test-Path $Vcpkg)) {
  try {
    $json = & $Vcpkg list --x-json "--x-install-root=$(Split-Path $VcpkgInstalle)" 2>$null | Out-String
    if ($LASTEXITCODE -eq 0 -and $json.Trim().StartsWith("{")) {
      $liste = $json | ConvertFrom-Json
      foreach ($p in $liste.PSObject.Properties) {
        $paquetsVcpkg += [pscustomobject]@{ nom = $p.Value.package_name; version = $p.Value.version; revisionPort = $p.Value.port_version; triplet = $p.Value.triplet }
      }
    }
  } catch { Write-Warning "Liste des paquets vcpkg indisponible : $_" }
  if ($paquetsVcpkg.Count -eq 0) {
    # Repli : le dossier « share » de chaque paquet installé, avec la version notée dans vcpkg.spdx.json
    Get-ChildItem (Join-Path $VcpkgInstalle "share") -Directory -ErrorAction SilentlyContinue | ForEach-Object {
      $f = Join-Path $_.FullName "vcpkg.spdx.json"
      if (Test-Path $f) {
        $d = Get-Content -Raw $f | ConvertFrom-Json
        $paquetsVcpkg += [pscustomobject]@{ nom = $_.Name; version = $d.packages[0].versionInfo; revisionPort = 0; triplet = "x64-windows" }
      }
    }
  }
  Get-ChildItem (Join-Path $VcpkgInstalle "share") -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    $f = Join-Path $_.FullName "vcpkg.spdx.json"
    if (Test-Path $f) { Copy-Item $f (Join-Path $sbom "vcpkg-$($_.Name).spdx.json") }
  }
}
$versionQt = (& (Join-Path $QtDir "bin\qtpaths.exe") --qt-version 2>$null)
if (-not $versionQt) { $versionQt = "inconnue" }
$cl = (Get-ChildItem (Join-Path $vs "VC\Tools\MSVC") -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$lignes = @("Prométhée $identifiant", "Révision des sources : $($v['revision'])", "Date de la révision : $($v['date'])",
  "Compilateur : MSVC $cl (Visual Studio $(Split-Path $vs -Leaf))", "Bibliothèques d'exécution Visual C++ : $(Split-Path (Split-Path (Split-Path $crt)) -Leaf)",
  "Qt : $versionQt", "vcpkg : $env:VCPKG_COMMIT", "", "Paquets vcpkg (x64-windows) :")
foreach ($p in $paquetsVcpkg | Sort-Object nom) { $lignes += "  $($p.nom) $($p.version)#$($p.revisionPort)" }
[IO.File]::WriteAllLines((Join-Path $dest "VERSIONS.txt"), $lignes, [Text.UTF8Encoding]::new($true))

$spdxPaquets = @(
  [ordered]@{ SPDXID = "SPDXRef-Promethee"; name = "Prométhée"; versionInfo = $identifiant; downloadLocation = "https://github.com/NitromWG/promethee";
              licenseConcluded = "GPL-3.0-only"; licenseDeclared = "GPL-3.0-only"; copyrightText = "NOASSERTION"; filesAnalyzed = $false },
  [ordered]@{ SPDXID = "SPDXRef-Qt"; name = "Qt"; versionInfo = $versionQt; downloadLocation = "https://download.qt.io/archive/qt/";
              licenseConcluded = "LGPL-3.0-only"; licenseDeclared = "LGPL-3.0-only"; copyrightText = "NOASSERTION"; filesAnalyzed = $false },
  [ordered]@{ SPDXID = "SPDXRef-MSVC-runtime"; name = "Microsoft Visual C++ Redistributable"; versionInfo = (Split-Path (Split-Path (Split-Path $crt)) -Leaf);
              downloadLocation = "NOASSERTION"; licenseConcluded = "LicenseRef-MSVC-Redistributable"; licenseDeclared = "LicenseRef-MSVC-Redistributable"; copyrightText = "NOASSERTION"; filesAnalyzed = $false },
  [ordered]@{ SPDXID = "SPDXRef-Clipper2"; name = "Clipper2"; versionInfo = "copie dans noyau/externe/clipper2"; downloadLocation = "https://github.com/AngusJohnson/Clipper2";
              licenseConcluded = "BSL-1.0"; licenseDeclared = "BSL-1.0"; copyrightText = "NOASSERTION"; filesAnalyzed = $false })
$relations = @([ordered]@{ spdxElementId = "SPDXRef-DOCUMENT"; relationshipType = "DESCRIBES"; relatedSpdxElement = "SPDXRef-Promethee" })
foreach ($id in "SPDXRef-Qt", "SPDXRef-MSVC-runtime", "SPDXRef-Clipper2") { $relations += [ordered]@{ spdxElementId = "SPDXRef-Promethee"; relationshipType = "DEPENDS_ON"; relatedSpdxElement = $id } }
$i = 0
foreach ($p in $paquetsVcpkg) {
  $i++
  $id = "SPDXRef-vcpkg-$i"
  $spdxPaquets += [ordered]@{ SPDXID = $id; name = $p.nom; versionInfo = "$($p.version)#$($p.revisionPort)"; downloadLocation = "NOASSERTION";
                              licenseConcluded = "NOASSERTION"; licenseDeclared = "NOASSERTION"; copyrightText = "NOASSERTION"; filesAnalyzed = $false;
                              comment = "Licence : voir licences/$($p.nom).txt et sbom/vcpkg-$($p.nom).spdx.json" }
  $relations += [ordered]@{ spdxElementId = "SPDXRef-Promethee"; relationshipType = "DEPENDS_ON"; relatedSpdxElement = $id }
}
$document = [ordered]@{
  spdxVersion = "SPDX-2.3"; dataLicense = "CC0-1.0"; SPDXID = "SPDXRef-DOCUMENT"; name = "Promethee-$identifiant-windows-x64"
  documentNamespace = "https://github.com/NitromWG/promethee/sbom/$identifiant"
  creationInfo = [ordered]@{ created = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ"); creators = @("Tool: outils/paquet-windows.ps1") }
  hasExtractedLicensingInfos = @([ordered]@{ licenseId = "LicenseRef-MSVC-Redistributable"; name = "Microsoft Visual C++ Redistributable";
                                             extractedText = "Fichiers redistribuables de Microsoft Visual Studio (liste Redist), déploiement local autorisé." })
  packages = $spdxPaquets; relationships = $relations
}
[IO.File]::WriteAllText((Join-Path $sbom "promethee.spdx.json"), ($document | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))

# 7. Vérification bloquante (dépendances, contenu), puis sommes SHA-256 de chaque fichier.
& (Join-Path $PSScriptRoot "verifier-paquet-windows.ps1") -Dossier $dest -Identifiant $identifiant -DossierOpenMP $script:dossierOpenMP
if ($LASTEXITCODE -ne 0) { throw "Vérification du paquet en échec" }
$sommes = Get-ChildItem $dest -Recurse -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | Sort-Object FullName | ForEach-Object {
  $rel = $_.FullName.Substring((Resolve-Path $dest).Path.Length + 1).Replace('\', '/')
  "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $rel"
}
[IO.File]::WriteAllLines((Join-Path $dest "SHA256SUMS.txt"), $sommes, [Text.UTF8Encoding]::new($false))
if ($env:GITHUB_OUTPUT) {
  "nom=$nomPaquet" | Out-File -Append -Encoding utf8 $env:GITHUB_OUTPUT
  "identifiant=$identifiant" | Out-File -Append -Encoding utf8 $env:GITHUB_OUTPUT
}
Write-Host "Paquet prêt : $dest"
