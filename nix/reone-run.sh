#!/usr/bin/env bash
# Run a reone app from a writable directory.
#
# The engine reads shaderpack.erf and reone.cfg from the current directory and
# writes engine.log there; the launcher saves reone.cfg there and starts
# "./engine"; the toolkit reads cmudict.dict from there. So every run happens
# inside REONE_HOME, which holds the user's reone.cfg and logs plus links back
# to the store. Saves are not here: the engine keeps them in <game dir>/saves.
#
# bin/reone, bin/reone-launcher and bin/reone-toolkit are symlinks to this
# script; the name it is called by picks the app. Called directly, the first
# argument does: reone-run engine|launcher|toolkit [args...]
#
# REONE_RUN_PRINT=1 prints the directory and command instead of running it
# (used by the tests).
set -euo pipefail

case "$(basename "$0")" in
  reone) app=engine ;;
  reone-launcher) app=launcher ;;
  reone-toolkit) app=toolkit ;;
  *)
    app=${1:-}
    shift || true
    ;;
esac
case "$app" in
  engine | launcher | toolkit) ;;
  *)
    echo "usage: reone-run engine|launcher|toolkit [args...]" >&2
    exit 2
    ;;
esac

store=@out@
home=${REONE_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/reone}
caller=$PWD

# Paths the user typed are relative to where they typed them, not to
# REONE_HOME. Only the engine takes a path option (--game).
absolute() {
  case "$1" in
    /*) printf '%s' "$1" ;;
    *) printf '%s/%s' "$caller" "$1" ;;
  esac
}

args=()
if [ "$app" = engine ]; then
  have_game=0
  while [ $# -gt 0 ]; do
    case "$1" in
      --game)
        have_game=1
        if [ $# -ge 2 ]; then
          args+=(--game "$(absolute "$2")")
          shift 2
          continue
        fi
        args+=("$1")
        ;;
      --game=*)
        have_game=1
        args+=("--game=$(absolute "${1#--game=}")")
        ;;
      *) args+=("$1") ;;
    esac
    shift
  done
  # Upstream defaults the game directory to the current directory, so
  # "cd ~/games/swkotor && reone" works. Keep that, unless reone.cfg (written
  # by the launcher) already names a game: a command-line --game would win
  # over it.
  if [ "$have_game" = 0 ] && ! grep -qs '^game=' "$home/reone.cfg"; then
    args+=(--game "$caller")
  fi
else
  args=("$@")
fi

mkdir -p "$home"
cd "$home"

# Refresh the links on every run so a new build is picked up. Never replace
# something that is not already a link: it is the user's file.
link() {
  if [ -e "$2" ] && [ ! -L "$2" ]; then
    echo "reone: $home/$2 exists and is not a link; move it away to continue" >&2
    exit 1
  fi
  ln -sfn "$1" "$2"
}
link "$store/share/reone/shaderpack.erf" shaderpack.erf
link "$store/libexec/reone/engine" engine
if [ -e "$store/share/reone/cmudict.dict" ]; then
  link "$store/share/reone/cmudict.dict" cmudict.dict
fi

if [ "${REONE_RUN_PRINT:-}" = 1 ]; then
  printf 'cwd=%s\n' "$PWD"
  printf 'exec=%s\n' "$store/libexec/reone/$app"
  printf 'arg=%s\n' ${args[@]+"${args[@]}"}
  exit 0
fi

exec "$store/libexec/reone/$app" ${args[@]+"${args[@]}"}
