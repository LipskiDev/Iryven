param([ValidateSet('All','Vulkan','DX12')][string]$Backend='All')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(!$env:VULKAN_SDK){throw 'Set VULKAN_SDK to the Vulkan SDK containing DXC and spirv-val'}
$dxc=Join-Path $env:VULKAN_SDK 'Bin/dxc.exe'
$validator=Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe'
$profiles=@{VERTEX=@('vs_6_6','vert');FRAGMENT=@('ps_6_6','frag');COMPUTE=@('cs_6_6','comp');MESH=@('ms_6_6','mesh');TASK=@('as_6_6','task')}
$backends=if($Backend -eq 'All'){@('DX12','Vulkan')}else{@($Backend)}
$failed=0;$count=0
foreach($source in Get-ChildItem (Join-Path $root 'assets/shaders/internal') -Filter '*.hlsl') {
    $metadata=[regex]::Match([IO.File]::ReadAllText($source.FullName),'IRYVEN_SHADER_STAGES:\s*([^\r\n]+)')
    if(!$metadata.Success){continue}
    foreach($stage in ($metadata.Groups[1].Value.Trim() -split '\s+')) {
    if(!$profiles.ContainsKey($stage)){throw "Unknown shader stage: $stage"}
    $profile=$profiles[$stage][0];$suffix=$profiles[$stage][1]
    foreach($target in $backends) {
        $destination=Join-Path $root "cache/shaders/validation/$target"
        New-Item -ItemType Directory -Force $destination | Out-Null
        $extension=if($target -eq 'DX12'){'.dxil'}else{'.spv'}
        $output=Join-Path $destination ($source.BaseName+'.'+$suffix+$extension)
        $arguments=@('-T',$profile,'-E','main',$source.FullName,'-Fo',$output)
        if($target -eq 'Vulkan'){$arguments+=@('-spirv','-fspv-target-env=vulkan1.3','-fvk-use-gl-layout')}
        & $dxc @arguments
        if($LASTEXITCODE){Write-Output "FAIL $($source.Name) $stage $target";$failed++;continue}
        if($target -eq 'Vulkan') {
            & $validator --target-env vulkan1.3 $output
            if($LASTEXITCODE){Write-Output "INVALID $($source.Name) SPIR-V";$failed++;continue}
        }
        Write-Output "PASS $($source.Name) $stage $target";$count++
    }
    }
}
if($failed){throw "$failed shader targets failed"}
Write-Output "Validated $count shader targets"
