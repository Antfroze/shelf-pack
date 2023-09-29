local import = import('smake/libraryInstaller')
import('smake/gpp', true)
import('smake/dependencyInstaller', true)
import('smake/dependencyIncluder', true)

local fs = import('smake/utils/fs')

function smake.install()
    InstallDependency('svg-format', function(installer)
        local folder = installer:GitClone('https://github.com/Antfroze/svg-format')

        folder:MoveIncludeFolder();

        folder:Delete()
    end)
end

function smake.build()
    smake.install()
    if not fs.Exists("out") then
        fs.CreateFolder("out")
    end

    standard('c++2a')

    inputr('src')
    include('include')

    IncludeDependencies('svg-format')

    output('out/shelf-pack')
    build()

    smake.run()
end

function smake.run()
    run('out/shelf-pack')
end
