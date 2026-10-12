param(
    [string]$Root,
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$main = Join-Path $Root 'src\main.cpp'
$resource = Join-Path $Root 'src\PAKNativePlugin.rc'
$document = Join-Path $Root 'docs\NativePlugin API Specification.md'
$quote = [char]34

$mainContent = [IO.File]::ReadAllText($main, [Text.UTF8Encoding]::new($false))
$mainContent = $mainContent -replace 'return \x22\d+\.\d+\.\d+\x22;', ('return ' + $quote + $Version + $quote + ';')
[IO.File]::WriteAllText($main, $mainContent, [Text.UTF8Encoding]::new($false))

$resourceVersion = $Version -replace '\.', ','
$resourceContent = [IO.File]::ReadAllText($resource, [Text.UTF8Encoding]::new($false))
$resourceContent = $resourceContent -replace '(FILEVERSION|PRODUCTVERSION) \d+,\d+,\d+,0', ('$1 ' + $resourceVersion + ',0')
$resourceContent = $resourceContent -replace '(VALUE \x22(?:File|Product)Version\x22, )\x22\d+\.\d+\.\d+\x22', ('$1' + $quote + $Version + $quote)
[IO.File]::WriteAllText($resource, $resourceContent, [Text.UTF8Encoding]::new($false))

$documentContent = [IO.File]::ReadAllText($document, [Text.UTF8Encoding]::new($false))
$documentContent = $documentContent -replace '\x60\x22\d+\.\d+\.\d+\x22\x60', ([char]96 + $quote + $Version + $quote + [char]96)
[IO.File]::WriteAllText($document, $documentContent, [Text.UTF8Encoding]::new($false))
