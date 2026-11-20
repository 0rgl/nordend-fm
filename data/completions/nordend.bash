# bash completion for nordend. bash-completion loads it from share/bash-completion/completions
# the first time nordend is completed; without bash-completion, source it from ~/.bashrc.
# Plain bash builtins only, so it does not depend on bash-completion's own functions.

_nordend() {
    local cur=${COMP_WORDS[COMP_CWORD]}
    local prev=${COMP_WORDS[COMP_CWORD - 1]}
    COMPREPLY=()

    # The values of the options that take some: free text, a number or a file.
    case $prev in
    --config)
        mapfile -t COMPREPLY < <(compgen -f -- "$cur")
        return
        ;;
    --title | --accept-label | --parent | --name | --file | --filter | --glob | --mime | \
        --current-filter | --choice | --option)
        return
        ;;
    esac
    # --choice takes three values and --option two.
    if ((COMP_CWORD >= 3)) && [[ ${COMP_WORDS[COMP_CWORD - 2]} == --choice ||
        ${COMP_WORDS[COMP_CWORD - 2]} == --option ]]; then
        return
    fi
    if ((COMP_CWORD >= 4)) && [[ ${COMP_WORDS[COMP_CWORD - 3]} == --choice ]]; then
        return
    fi

    local word pick='' paths='' only_paths=''
    for word in "${COMP_WORDS[@]:1:COMP_CWORD-1}"; do
        case $word in
        --) only_paths=1 ;;
        --pick) pick=1 ;;
        --reveal | --properties) paths=1 ;;
        esac
    done

    if [[ -z $only_paths && $cur == -* ]]; then
        local options='--config --check-config --test-scene --version --about --reveal
            --properties --set-file-chooser --unset-file-chooser --set-file-manager
            --unset-file-manager --pick --'
        if [[ -n $pick ]]; then
            options+=' --multiple --folders --title --accept-label --filter --glob --mime
                --current-filter --parent --save --name --file --choice --option'
        fi
        mapfile -t COMPREPLY < <(compgen -W "$options" -- "$cur")
        return
    fi
    # --reveal and --properties take any paths; otherwise a folder to show, also after --.
    if [[ -n $paths ]]; then
        mapfile -t COMPREPLY < <(compgen -f -- "$cur")
    else
        mapfile -t COMPREPLY < <(compgen -d -- "$cur")
    fi
} &&
    complete -o filenames -F _nordend nordend
