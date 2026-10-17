# Pester 5 tests for tirevive-bundles.ps1 (E186: setup.exe entries left behind by direct .msi installs).
# Run: native ctest (test installer_bundles, needs pwsh + Pester 5) or Invoke-Pester src/installer/tests.
BeforeAll {
    . (Join-Path $PSScriptRoot '..' 'tirevive-bundles.ps1')
    $ours = '{B785EEA0-32B6-4B72-BD0E-62ADA99417BA}'
    $old = '{3956D8EC-442D-405D-961B-47BCB40DFA84}'   # the 0.8.0 bundle on the bare-metal PC
    $aborted = '{61F11BA7-526F-46F2-8D56-D15EB283A195}' # a setup.exe closed at its first dialog
    function Bundle($id, $codes) { [pscustomobject]@{ Id = $id; UpgradeCodes = @($codes) } }
    function Provider($name, $dependents, $display = 'TI Revive') {
        [pscustomobject]@{ Name = $name; DisplayName = $display; Dependents = @($dependents) }
    }
    function Names($plan, $kind) { @($plan | Where-Object Kind -eq $kind | ForEach-Object { $_.Name -join '\' }) }
}

Describe 'Get-StaleBundlePlan' {
    It 'removes every registration of a TI Revive bundle' {
        $plan = Get-StaleBundlePlan @((Bundle $old $ours)) @()
        Names $plan 'Uninstall' | Should -Be @($old)
        Names $plan 'Dependencies' | Should -Be @($old)
        Names $plan 'Cache' | Should -Be @($old)
    }

    It 'leaves bundles of other products and plain MSI entries alone' {
        $other = '{11111111-2222-3333-4444-555555555555}'
        $plan = Get-StaleBundlePlan @((Bundle $other '{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}'), (Bundle '{FDCC9F97-5020-4CC9-9422-3BB302DB75E8}' @())) @()
        @($plan).Count | Should -Be 0
    }

    It 'matches the upgrade code in any case and among several codes' {
        $plan = Get-StaleBundlePlan @((Bundle $old @('{00000000-0000-0000-0000-000000000000}', $ours.ToLower()))) @()
        Names $plan 'Uninstall' | Should -Be @($old)
    }

    It 'ignores entry names that are not a GUID (never a path outside the uninstall key)' {
        $plan = Get-StaleBundlePlan @((Bundle '..\..\Run' $ours), (Bundle '' $ours)) @()
        @($plan).Count | Should -Be 0
    }

    It 'removes the MSI package provider and cached MSI that only stale bundles depended on' {
        $plan = Get-StaleBundlePlan @((Bundle $old $ours)) @((Provider '{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0' @($old)))
        Names $plan 'Provider' | Should -Be @('{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0')
        Names $plan 'Cache' | Should -Be @($old, '{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}v0.8.0')
    }

    It 'only drops the stale dependents of a provider another bundle still uses' {
        $keep = '{22222222-3333-4444-5555-666666666666}'
        $plan = Get-StaleBundlePlan @((Bundle $old $ours)) @((Provider '{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0' @($old, $keep)))
        Names $plan 'Provider' | Should -Be @()
        Names $plan 'Dependent' | Should -Be @("{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0\Dependents\$old")
    }

    It 'keeps providers without stale dependents and providers of other products' {
        $providers = @((Provider '{FDCC9F97-5020-4CC9-9422-3BB302DB75E8}_v0.8.20' @()),
            (Provider '{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0' @($old) 'Something else'),
            (Provider 'Microsoft.VCRedist' @($old)))
        $plan = Get-StaleBundlePlan @((Bundle $old $ours)) $providers
        Names $plan 'Provider' | Should -Be @()
        Names $plan 'Dependent' | Should -Be @()
    }

    It 'handles the bare-metal PC of 2026-10-17 (0.8.0 bundle + aborted 0.8.20 setup.exe + direct MSI)' {
        $bundles = @((Bundle $old $ours), (Bundle $aborted $ours), (Bundle '{FDCC9F97-5020-4CC9-9422-3BB302DB75E8}' @()))
        $providers = @((Provider $old @($old)), (Provider $aborted @($aborted)),
            (Provider '{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0' @($old)),
            (Provider '{FDCC9F97-5020-4CC9-9422-3BB302DB75E8}_v0.8.20' @()))
        $plan = Get-StaleBundlePlan $bundles $providers
        Names $plan 'Uninstall' | Should -Be @($old, $aborted)
        Names $plan 'Provider' | Should -Be @('{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0')
    }
}

Describe 'Invoke-StaleBundlePlan' {
    It 'removes the planned keys and folders and nothing else' {
        $u = New-Item -ItemType Directory (Join-Path $TestDrive 'Uninstall')
        $d = New-Item -ItemType Directory (Join-Path $TestDrive 'Dependencies')
        $c = New-Item -ItemType Directory (Join-Path $TestDrive 'Cache')
        foreach ($p in @("$u/$old", "$u/keep", "$d/$old", "$d/prov_v1/Dependents/$old", "$d/prov_v1/Dependents/other",
                "$c/$old", "$c/keep")) { New-Item -ItemType Directory -Force $p | Out-Null }
        $plan = @([pscustomobject]@{ Kind = 'Uninstall'; Name = @($old) },
            [pscustomobject]@{ Kind = 'Dependencies'; Name = @($old) },
            [pscustomobject]@{ Kind = 'Dependent'; Name = @('prov_v1', 'Dependents', $old) },
            [pscustomobject]@{ Kind = 'Cache'; Name = @($old) },
            [pscustomobject]@{ Kind = 'Cache'; Name = @('{00000000-0000-0000-0000-000000000000}') }) # absent: no error
        Invoke-StaleBundlePlan $plan $u $d $c | Out-Null
        Test-Path "$u/$old" | Should -BeFalse
        Test-Path "$d/$old" | Should -BeFalse
        Test-Path "$d/prov_v1/Dependents/$old" | Should -BeFalse
        Test-Path "$c/$old" | Should -BeFalse
        Test-Path "$u/keep" | Should -BeTrue
        Test-Path "$d/prov_v1/Dependents/other" | Should -BeTrue
        Test-Path "$c/keep" | Should -BeTrue
    }
}

Describe 'registry readers' -Skip:(-not $IsWindows) {
    It 'read bundles and providers the way Burn writes them' {
        $u = New-Item 'TestRegistry:\Uninstall'
        $b = New-Item "TestRegistry:\Uninstall\$old"
        New-ItemProperty $b.PSPath -Name BundleUpgradeCode -PropertyType MultiString -Value @($ours) | Out-Null
        New-Item 'TestRegistry:\Uninstall\{FDCC9F97-5020-4CC9-9422-3BB302DB75E8}' | Out-Null # MSI: no upgrade code
        $d = New-Item 'TestRegistry:\Dependencies'
        $p = New-Item 'TestRegistry:\Dependencies\{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0'
        New-ItemProperty $p.PSPath -Name DisplayName -Value 'TI Revive' | Out-Null
        New-Item "TestRegistry:\Dependencies\{A9E255AC-35DE-4C5C-AC9D-66BE6C1C28B6}_v0.8.0\Dependents\$old" -Force | Out-Null
        $bundles = @(Get-BundleEntries $u.PSPath)
        $bundles.Count | Should -Be 1
        $bundles[0].Id | Should -Be $old
        $bundles[0].UpgradeCodes | Should -Be @($ours)
        $providers = @(Get-ProviderEntries $d.PSPath)
        $providers[0].DisplayName | Should -Be 'TI Revive'
        $providers[0].Dependents | Should -Be @($old)
    }
}
