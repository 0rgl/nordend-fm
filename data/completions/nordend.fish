# fish completion for nordend, loaded from share/fish/vendor_completions.d.

# Without options, a folder to show; files only after --reveal and --properties.
complete -c nordend -f
complete -c nordend -n 'not __fish_contains_opt reveal properties' -a '(__fish_complete_directories)'
complete -c nordend -n '__fish_contains_opt reveal properties' -F

complete -c nordend -l config -r -F -d 'Read this file instead of config.toml'
complete -c nordend -l check-config -d 'Report problems in the config file and exit'
complete -c nordend -l test-scene -d 'Show the renderer test scene'
complete -c nordend -l version -d 'Print the version and exit'
complete -c nordend -l about -d 'Print what Nordend runs on and exit'
complete -c nordend -l reveal -d 'Show each path selected in its folder'
complete -c nordend -l properties -d 'Same as --reveal, with the details open'
complete -c nordend -l set-file-chooser -d 'Make Nordend the file chooser and exit'
complete -c nordend -l unset-file-chooser -d 'Give the file chooser back and exit'
complete -c nordend -l set-file-manager -d 'Make "Show in Folder" open Nordend and exit'
complete -c nordend -l unset-file-manager -d 'Give "Show in Folder" back and exit'
complete -c nordend -l pick -d 'Choose a file and print its URI'

# What goes with --pick, offered once it is there.
set -l pick '__fish_contains_opt pick'
complete -c nordend -n $pick -l multiple -d 'Choose several files'
complete -c nordend -n $pick -l folders -d 'Choose folders instead of files'
complete -c nordend -n $pick -l title -x -d 'Window title'
complete -c nordend -n $pick -l accept-label -x -d 'Label of the choosing button'
complete -c nordend -n $pick -l filter -x -d 'Offer a filter, of the --glob and --mime after it'
complete -c nordend -n $pick -l glob -x -d 'A pattern of the last --filter'
complete -c nordend -n $pick -l mime -x -d 'A MIME type of the last --filter'
complete -c nordend -n $pick -l current-filter -x -d 'The filter in use at first (0 = first)'
complete -c nordend -n $pick -l parent -x -d 'The application window (wayland:HANDLE)'
complete -c nordend -n $pick -l save -d 'Save instead of open'
complete -c nordend -n $pick -l name -x -d 'The name proposed for saving'
complete -c nordend -n $pick -l file -x -d 'A file saved into the folder chosen'
complete -c nordend -n $pick -l choice -x -d 'A choice: ID LABEL DEFAULT'
complete -c nordend -n $pick -l option -x -d 'An option of the last --choice: ID LABEL'
