#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# This tool generates full update packages for the update system.
# Author: Darin Fisher
#

# -----------------------------------------------------------------------------
# By default just assume that these tools exist in our path
MAR=${MAR:-mar}
MBSDIFF=${MBSDIFF:-mbsdiff}
if [[ -z "${MAR_OLD_FORMAT}" ]]; then
  XZ=${XZ:-xz}
else
  MAR_OLD_FORMAT=1
  BZIP2=${BZIP2:-bzip2}
fi

# -----------------------------------------------------------------------------
# Helper routines

notice() {
  echo "$*" 1>&2
}

get_file_size() {
  info=($(ls -ln "$1"))
  echo ${info[4]}
}

copy_perm() {
  reference="$1"
  target="$2"

  if [ -x "$reference" ]; then
    chmod 0755 "$target"
  else
    chmod 0644 "$target"
  fi
}

make_add_instruction() {
  f="$1"
  filev2="$2"
  # The third param will be an empty string when a file add instruction is only
  # needed in the version 2 manifest. This only happens when the file has an
  # add-if-not instruction in the version 3 manifest. This is due to the
  # precomplete file prior to the version 3 manifest having a remove instruction
  # for this file so the file is removed before applying a complete update.
  filev3="$3"

  # Used to log to the console
  if [ $4 ]; then
    forced=" (forced)"
  else
    forced=
  fi

  is_extension=$(echo "$f" | grep -c 'distribution/extensions/.*/')
  if [ $is_extension = "1" ]; then
    # Use the subdirectory of the extensions folder as the file to test
    # before performing this add instruction.
    testdir=$(echo "$f" | sed 's/\(.*distribution\/extensions\/[^\/]*\)\/.*/\1/')
    notice "     add-if \"$testdir\" \"$f\""
    echo "add-if \"$testdir\" \"$f\"" >> "$filev2"
    if [ ! $filev3 = "" ]; then
      echo "add-if \"$testdir\" \"$f\"" >> "$filev3"
    fi
  else
    notice "        add \"$f\"$forced"
    echo "add \"$f\"" >> "$filev2"
    if [ ! "$filev3" = "" ]; then
      echo "add \"$f\"" >> "$filev3"
    fi
  fi
}

check_for_add_if_not_update() {
  add_if_not_file_chk="$1"

  if [ `basename $add_if_not_file_chk` = "update-settings.ini" ]; then
    ## "true" *giggle*
    return 0;
  fi
  ## 'false'... because this is bash. Oh yay!
  return 1;
}

check_for_add_to_manifestv2() {
  add_if_not_file_chk="$1"

  if [ `basename $add_if_not_file_chk` = "update-settings.ini" ]; then
    ## "true" *giggle*
    return 0;
  fi
  ## 'false'... because this is bash. Oh yay!
  return 1;
}

make_add_if_not_instruction() {
  f="$1"
  filev3="$2"

  notice " add-if-not \"$f\" \"$f\""
  echo "add-if-not \"$f\" \"$f\"" >> "$filev3"
}

make_patch_instruction() {
  f="$1"
  filev2="$2"
  filev3="$3"

  is_extension=$(echo "$f" | grep -c 'distribution/extensions/.*/')
  if [ $is_extension = "1" ]; then
    # Use the subdirectory of the extensions folder as the file to test
    # before performing this add instruction.
    testdir=$(echo "$f" | sed 's/\(.*distribution\/extensions\/[^\/]*\)\/.*/\1/')
    notice "   patch-if \"$testdir\" \"$f.patch\" \"$f\""
    echo "patch-if \"$testdir\" \"$f.patch\" \"$f\"" >> "$filev2"
    echo "patch-if \"$testdir\" \"$f.patch\" \"$f\"" >> "$filev3"
  else
    notice "      patch \"$f.patch\" \"$f\""
    echo "patch \"$f.patch\" \"$f\"" >> "$filev2"
    echo "patch \"$f.patch\" \"$f\"" >> "$filev3"
  fi
}

append_remove_instructions() {
  dir="$1"
  filev2="$2"
  filev3="$3"

  if [ -f "$dir/removed-files" ]; then
    listfile="$dir/removed-files"
  elif [ -f "$dir/Contents/Resources/removed-files" ]; then
    listfile="$dir/Contents/Resources/removed-files"
  fi
  if [ -n "$listfile" ]; then
    # Map spaces to pipes so that we correctly handle filenames with spaces.
    files=($(cat "$listfile" | tr " " "|"  | sort -r))
    num_files=${#files[*]}
    for ((i=0; $i<$num_files; i=$i+1)); do
      # Map pipes back to whitespace and remove carriage returns
      f=$(echo ${files[$i]} | tr "|" " " | tr -d '\r')
      # Trim whitespace
      f=$(echo $f)
      # Exclude blank lines.
      if [ -n "$f" ]; then
        # Exclude comments
        if [ ! $(echo "$f" | grep -c '^#') = 1 ]; then
          if [ $(echo "$f" | grep -c '\/$') = 1 ]; then
            notice "      rmdir \"$f\""
            echo "rmdir \"$f\"" >> "$filev2"
            echo "rmdir \"$f\"" >> "$filev3"
          elif [ $(echo "$f" | grep -c '\/\*$') = 1 ]; then
            # Remove the *
            f=$(echo "$f" | sed -e 's:\*$::')
            notice "    rmrfdir \"$f\""
            echo "rmrfdir \"$f\"" >> "$filev2"
            echo "rmrfdir \"$f\"" >> "$filev3"
          else
            notice "     remove \"$f\""
            echo "remove \"$f\"" >> "$filev2"
            echo "remove \"$f\"" >> "$filev3"
          fi
        fi
      fi
    done
  fi
}

# List all files in the current directory, stripping leading "./"
# Pass a variable name and it will be filled as an array.
list_files() {
  count=0

  find . -type f \
    ! -name "update.manifest" \
    ! -name "updatev2.manifest" \
    ! -name "updatev3.manifest" \
    ! -name "temp-dirlist" \
    ! -name "temp-filelist" \
    | sed 's/\.\/\(.*\)/\1/' \
    | sort -r > "temp-filelist"
  while read file; do
    eval "${1}[$count]=\"$file\""
    (( count++ ))
  done < "temp-filelist"
  rm "temp-filelist"
}

# List all directories in the current directory, stripping leading "./"
list_dirs() {
  count=0

  find . -type d \
    ! -name "." \
    ! -name ".." \
    | sed 's/\.\/\(.*\)/\1/' \
    | sort -r > "temp-dirlist"
  while read dir; do
    eval "${1}[$count]=\"$dir\""
    (( count++ ))
  done < "temp-dirlist"
  rm "temp-dirlist"
}

# -----------------------------------------------------------------------------

print_usage() {
  notice "Usage: $(basename $0) [OPTIONS] ARCHIVE DIRECTORY"
}

if [ $# = 0 ]; then
  print_usage
  exit 1
fi

if [ $1 = -h ]; then
  print_usage
  notice ""
  notice "The contents of DIRECTORY will be stored in ARCHIVE."
  notice ""
  notice "Options:"
  notice "  -h  show this help text"
  notice ""
  exit 1
fi

# -----------------------------------------------------------------------------

archive="$1"
targetdir="$2"
# Prevent the workdir from being inside the targetdir so it isn't included in
# the update mar.
if [ $(echo "$targetdir" | grep -c '\/$') = 1 ]; then
  # Remove the /
  targetdir=$(echo "$targetdir" | sed -e 's:\/$::')
fi
workdir="$targetdir.work"
updatemanifestv2="$workdir/updatev2.manifest"
updatemanifestv3="$workdir/updatev3.manifest"
targetfiles="updatev2.manifest updatev3.manifest"

mkdir -p "$workdir"

# Generate a list of all files in the target directory.
pushd "$targetdir"
if test $? -ne 0 ; then
  exit 1
fi

if [ ! -f "precomplete" ]; then
  if [ ! -f "Contents/Resources/precomplete" ]; then
    notice "precomplete file is missing!"
    exit 1
  fi
fi

list_files files

popd

# Add the type of update to the beginning of the update manifests.
> "$updatemanifestv2"
> "$updatemanifestv3"
notice ""
notice "Adding type instruction to update manifests"
notice "       type complete"
echo "type \"complete\"" >> "$updatemanifestv2"
echo "type \"complete\"" >> "$updatemanifestv3"

notice ""
notice "Adding file add instructions to update manifests"
num_files=${#files[*]}

for ((i=0; $i<$num_files; i=$i+1)); do
  f="${files[$i]}"

  if check_for_add_if_not_update "$f"; then
    make_add_if_not_instruction "$f" "$updatemanifestv3"
    if check_for_add_to_manifestv2 "$f"; then
      make_add_instruction "$f" "$updatemanifestv2" "" 1
    fi
  else
    make_add_instruction "$f" "$updatemanifestv2" "$updatemanifestv3"
  fi

  dir=$(dirname "$f")
  mkdir -p "$workdir/$dir"
  if [[ -n $MAR_OLD_FORMAT ]]; then
    $BZIP2 -cz9 "$targetdir/$f" > "$workdir/$f"
  else
    $XZ --compress --x86 --lzma2 --format=xz --check=crc64 --force --stdout "$targetdir/$f" > "$workdir/$f"
  fi
  copy_perm "$targetdir/$f" "$workdir/$f"

  targetfiles="$targetfiles \"$f\""
done

# Append remove instructions for any dead files.
notice ""
notice "Adding file and directory remove instructions from file 'removed-files'"
append_remove_instructions "$targetdir" "$updatemanifestv2" "$updatemanifestv3"

if [[ -n $MAR_OLD_FORMAT ]]; then
  $BZIP2 -z9 "$updatemanifestv2" && mv -f "$updatemanifestv2.bz2" "$updatemanifestv2"
  $BZIP2 -z9 "$updatemanifestv3" && mv -f "$updatemanifestv3.bz2" "$updatemanifestv3"
else
  $XZ --compress --x86 --lzma2 --format=xz --check=crc64 --force "$updatemanifestv2" && mv -f "$updatemanifestv2.xz" "$updatemanifestv2"
  $XZ --compress --x86 --lzma2 --format=xz --check=crc64 --force "$updatemanifestv3" && mv -f "$updatemanifestv3.xz" "$updatemanifestv3"
fi

eval "$MAR -C \"$workdir\" -c output.mar $targetfiles"
mv -f "$workdir/output.mar" "$archive"

# cleanup
rm -fr "$workdir"

notice ""
notice "Finished"
notice ""
