$ErrorActionPreference='Stop'
$baseVersion=(Get-Content 'QtScrcpy/appversion' -Raw).Trim()
if($baseVersion -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid application version'}
$version="$baseVersion-rc.1"
$name="QtScrcpy-keymapper-$version-win64"
$package=Join-Path $PWD "dist/$name"
New-Item $package -ItemType Directory -Force | Out-Null
Copy-Item 'output/x64/RelWithDebInfo/*' $package -Recurse
Copy-Item 'config','keymap' $package -Recurse
Copy-Item 'LICENSE' (Join-Path $package 'LICENSE-QtScrcpy')
Copy-Item 'QtScrcpy/QtScrcpyCore/LICENSE' (Join-Path $package 'LICENSE-QtScrcpyCore')
Copy-Item 'docs/keymapper-integration.zh-CN.md' (Join-Path $package 'README-Keymapper.zh-CN.md')
Copy-Item 'docs/application-profiles.zh-CN.md' (Join-Path $package 'README-Applications.zh-CN.md')
Copy-Item 'docs/release-0.4.5.zh-CN.md' (Join-Path $package 'README-0.4.5.zh-CN.md')
Copy-Item 'docs/release-0.4.4.zh-CN.md' (Join-Path $package 'README-0.4.4.zh-CN.md')
Copy-Item 'docs/release-0.4.3.zh-CN.md' (Join-Path $package 'README-0.4.3.zh-CN.md')
Copy-Item 'docs/release-0.4.6.zh-CN.md' (Join-Path $package 'README-0.4.6.zh-CN.md')
Copy-Item 'docs/release-0.4.7.zh-CN.md' (Join-Path $package 'README-0.4.7.zh-CN.md')
Copy-Item 'docs/release-0.4.8.zh-CN.md' (Join-Path $package 'README-0.4.8.zh-CN.md')
if(!(Test-Path (Join-Path $package 'qtscrcpy-cursor.jar'))){throw 'Missing phone cursor helper'}
Copy-Item 'docs/device-rotation.zh-CN.md' (Join-Path $package 'README-Rotation.zh-CN.md')
Get-ChildItem $package -Include '*.lib','*.pdb','*.exp' -Recurse | Remove-Item
windeployqt --release (Join-Path $package 'QtScrcpy.exe')
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
foreach($dll in @('msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll')){Copy-Item (Join-Path $env:WINDIR "System32/$dll") $package}
if(!(Test-Path (Join-Path $package 'QtWebEngineProcess.exe'))){throw 'Missing QtWebEngineProcess.exe'}
if(!(Test-Path (Join-Path $package 'resources/qtwebengine_resources.pak'))){throw 'Missing WebEngine resources'}
$qtPrefix=(& qmake -query QT_INSTALL_PREFIX).Trim()
$localeDir=Join-Path $package 'translations/qtwebengine_locales'
New-Item $localeDir -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $qtPrefix 'translations/qtwebengine_locales/en-US.pak') $localeDir
[IO.File]::WriteAllText((Join-Path $package 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=.`nLibraryExecutables=.`nData=.`nTranslations=translations`n")
foreach($entry in @(@{Name='upstream';Root=(Join-Path $PWD 'QtScrcpy/QtScrcpyCore/src/third_party')},@{Name='Qt';Root=$qtPrefix})){
  Get-ChildItem $entry.Root -File -Recurse | Where-Object {$_.Name -match '^(LICENSE|COPYING|NOTICE|COPYRIGHT)(\.|$|-)'} | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath($entry.Root,$_.FullName)
    $target=Join-Path $package "licenses/$($entry.Name)/$relative"
    New-Item (Split-Path $target) -ItemType Directory -Force | Out-Null
    Copy-Item $_.FullName $target
  }
}
New-Item (Join-Path $package 'licenses/ScrcpyKeyMapper') -ItemType Directory -Force | Out-Null
Copy-Item 'QtScrcpy/third_party/ScrcpyKeyMapper/LICENSE' (Join-Path $package 'licenses/ScrcpyKeyMapper/LICENSE')
Copy-Item 'build-keymapper/QtScrcpy/keymapper/assets/licenses/*' (Join-Path $package 'licenses/ScrcpyKeyMapper')
Copy-Item 'build-keymapper/QtScrcpy/keymapper/assets/UPSTREAM.txt' (Join-Path $package 'licenses/ScrcpyKeyMapper/UPSTREAM.txt')
$mainSha=git rev-parse HEAD
$coreSha=git -C QtScrcpy/QtScrcpyCore rev-parse HEAD
$editorSha=git -C QtScrcpy/third_party/ScrcpyKeyMapper rev-parse HEAD
$testExe=Join-Path $package 'keymapper_smoke.exe'
Copy-Item 'build-keymapper/QtScrcpy/keymapper/RelWithDebInfo/keymapper_smoke.exe' $testExe
$ctest=(Get-Command ctest -ErrorAction Stop).Source
$portableTestDir=Join-Path $PWD 'build-keymapper/portable-test'
New-Item $portableTestDir -ItemType Directory -Force | Out-Null
$cmakeTestExe=$testExe.Replace('\','/')
$cmakePackage=$package.Replace('\','/')
[IO.File]::WriteAllText((Join-Path $portableTestDir 'CTestTestfile.cmake'), "add_test(portable_editor `"$cmakeTestExe`")`nset_tests_properties(portable_editor PROPERTIES TIMEOUT 45 WORKING_DIRECTORY `"$cmakePackage`")`n")
$savedPath=$env:PATH
$saved=@{}
foreach($key in @('QT_PLUGIN_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORM','QTWEBENGINEPROCESS_PATH','QTWEBENGINE_RESOURCES_PATH','QTWEBENGINE_LOCALES_PATH')){
  $saved[$key]=[Environment]::GetEnvironmentVariable($key)
  [Environment]::SetEnvironmentVariable($key,$null)
}
try {
  $env:PATH="$package;$env:WINDIR/System32;$env:WINDIR"
  $env:QSC_KEYMAPPER_SCREENSHOT=Join-Path $PWD 'build-keymapper/portable-editor.png'
  # CTest owns the GUI test and timeout; hidden startup suppresses WebEngine input focus.
  & $ctest --test-dir $portableTestDir --output-on-failure --output-junit results.xml --timeout 45
  if($LASTEXITCODE -ne 0){throw 'Portable editor failed'}
  $p=Start-Process (Join-Path $package 'QtScrcpy.exe') -WorkingDirectory $package -WindowStyle Hidden -PassThru
  Start-Sleep -Seconds 5
  if($p.HasExited){throw "Startup failed: $($p.ExitCode)"}
  Stop-Process -Id $p.Id
} finally {
  $env:PATH=$savedPath
  foreach($key in $saved.Keys){[Environment]::SetEnvironmentVariable($key,$saved[$key])}
  Remove-Item $testExe -ErrorAction SilentlyContinue
}
if(Get-ChildItem $package -Recurse -File | Where-Object {$_.Extension -match '^\.(ttf|otf|woff|woff2|ttc)$'}){throw 'Unexpected standalone font file in package'}
@{phoneCursor=$true;phoneCursorProtocol=1;toolbarWheelRouting=$true;integratedToolbar=$true;splitSystemPanels=$true;version=$version;mainCommit=$mainSha;coreCommit=$coreSha;editorCommit=$editorSha;runId=$env:GITHUB_RUN_ID;newFeaturesDeviceTested=$false;maximumPlaybackSpeed=8;audioChanged=$false;embeddedOfflineEditor=$true;portableEditorVerified=$true;stableViewOrientation=$true;preserveViewWindowGeometry=$true;manualRotationKeepsAxis=$true;inlineAppClose=$true;taskParserDiagnostics=$true;nativeTabScrollButtons=$false;viewRotation=$true;recentTaskTabs=$true;closeRecentApplications=$true;deviceRotationMenu=$true;rotationBackupRestore=$true;applicationTabs=$true;applicationProfiles=$true;macroForegroundRecovery=$true} | ConvertTo-Json | Set-Content (Join-Path $package 'build-info.json') -Encoding utf8
$zip=Join-Path $PWD "dist/$name.zip"
Compress-Archive $package $zip
$hash=(Get-FileHash $zip -Algorithm SHA256).Hash
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip`n")
Write-Output "Package SHA-256: $hash"
