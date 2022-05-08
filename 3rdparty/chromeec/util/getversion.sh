#!/bin/bash
#
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Generate version information for the EC binary

# Use this symbol as a separator to be able to reliably concatenate strings of
# text.
dc=$'\001'

# Default marker to indicate 'dirty' repositories
dirty_marker='+'

# This function examines the state of the current directory and attempts to
# extract its version information: the latest tag, if any, how many patches
# are there since the latest tag, the top sha1, and if there are local
# modifications.
#
# Local modifications are reported by concatenating the revision string and
# the string '-dirty' using the $dc symbol as the separator.
#
# If there is no tags defined in this git repository, the base version is
# considered to be 0.0.
#
# If current directory is not a git depository, this function prints out
# "no_version"

get_tree_version() {
  local dirty
  local ghash
  local numcommits
  local tag
  local vbase
  local ver_branch
  local ver_major

  if ghash=`git rev-parse --short --verify HEAD 2>/dev/null`; then
    if gdesc=`git describe --dirty --match='v*' 2>/dev/null`; then
      IFS="-" fields=($gdesc)
      tag="${fields[0]}"
      IFS="." vernum=($tag)
      numcommits=$((${vernum[2]}+${fields[1]:-0}))
      ver_major="${vernum[0]}"
      ver_branch="${vernum[1]}"
    else
      numcommits=`git rev-list HEAD | wc -l`
      ver_major="v0"
      ver_branch="0"
    fi
    # avoid putting the -dirty attribute if only the timestamp
    # changed
    git status > /dev/null 2>&1

    if [ -n "$(git diff-index --name-only HEAD 2>/dev/null)" ]; then
      dirty="${dirty_marker}"
    fi
    vbase="${ver_major}.${ver_branch}.${numcommits}-${ghash}${dirty}"
  else
    # Fall back to the VCSID provided by the packaging system if available.
    if ghash=${VCSID##*-}; then
      vbase="1.1.9999-${ghash:0:7}"
    else
      # then ultimately fails to "no_version"
      vbase="no_version"
    fi
  fi
  echo "${vbase}${dc}${dirty}"
}


IFS="${dc}"
ver="${CR50_DEV:+DBG/}${BOARD}_"
global_dirty=    # set if any of the component repos is 'dirty'.
dir_list=( . )   # list of component directories, always includes the EC tree

case "${BOARD}" in
  (cr50)
    dir_list+=( ../../third_party/tpm2 ../../third_party/cryptoc )
    ;;
esac

# Create a combined version string for all component directories.
for git_dir in ${dir_list[@]}; do
  pushd "${git_dir}" > /dev/null
  component="$(basename "${git_dir}")"
  values=( $(get_tree_version) )
  vbase="${values[0]}"             # Retrieved version information.
  global_dirty+="${values[1]}"     # Non-zero, if the repository is 'dirty'
  if [ "${component}" != "." ]; then
    ver+=" ${component}:"
  fi
  ver+="${vbase}"
  popd > /dev/null
done

# On some boards where the version number consists of multiple components we
# want to separate the first word of the version string as the version of the
# EC tree.
IFS=' ' first_word=(${ver})

echo "/* This file is generated by util/getversion.sh */"

echo "/* Version string for use by common/version.c */"
echo "#define CROS_EC_VERSION \"${ver}\""

echo "/* Version string, truncated to 31 chars (+ terminating null = 32) */"
echo "#define CROS_EC_VERSION32 \"${first_word:0:31}\""

echo "/* Sub-fields for use in Makefile.rules and to form build info string"
echo " * in common/version.c. */"
echo "#define VERSION \"${ver}\""
echo "#define BUILDER \"${USER}@`hostname`\""

if [ -n "$global_dirty" ]; then
    echo "/* Repo is dirty, using time of last compilation */"
    echo "#define DATE \"$(date '+%F %T')\""
else
    echo "/* Repo is clean, use the commit date of the last commit */"
    # If called from an ebuild we won't have a git repo, so redirect stderr
    # to avoid annoying 'Not a git repository' errors.
    gitdate=$(git log -1 --format='%ci' HEAD 2>/dev/null | cut -d ' ' -f '1 2')
    echo "#define DATE \"${gitdate}\""
fi
