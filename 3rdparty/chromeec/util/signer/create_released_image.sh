#!/bin/bash

#
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script is a utility which allows to sign dev and prod CR50 images for
# release and place them in a tarball suitable for uploading to the BCS.
#
# Manifests present in the EC directory at the time of signing are used for
# both prod and dev images.
#

set -u
set -e

# A very crude RO verification function. The key signature found at a fixed
# offset into the RO blob must match the RO type. Prod keys have bit D2 set to
# one, dev keys have this bit set to zero.
verify_ro() {
  local ro_bin="${1}"
  local type_expected="${2}"
  local key_byte

  if [ ! -f "${ro_bin}" ]; then
    echo "${ro_bin} not a file!" >&2
    exit 1
  fi

  # Key signature's lowest byte is byte #5 in the line at offset 0001a0.
  key_byte="$(od -Ax -t x1 -v "${ro_bin}" | awk '/0001a0/ {print $6};')"
  case "${key_byte}" in
    (?[4567cdef])
      if [ "${type_expected}" == "prod" ]; then
        return 0
      fi
      ;;
    (?[012389ab])
      if [ "${type_expected}" == "dev" ]; then
        return 0
      fi
      ;;
  esac

  echo "RO key in ${ro_bin} does not match type ${type_expected}" >&2
  exit 1
}

# This function prepares a full CR50 image, consisting of two ROs and two RWs
# placed at their respective offsets into the resulting blob. It invokes the
# bs (binary signer) script to actually convert elf versions of RWs into
# binaries and sign them.
#
# The signed image is placed in the directory named as concatenation of RO and
# RW version numbers, which is where eventually the ebuild downloading the
# tarball from the BCS expects the dev and prod images be.
prepare_image() {
  local count=0
  local extra_param=
  local image_type="${1}"
  local ro_a_hex="$(readlink -f "${2}")"
  local ro_b_hex="$(readlink -f "${3}")"
  local rw_a="$(readlink -f "${4}")"
  local rw_b="$(readlink -f "${5}")"
  local version

  for f in "${ro_a_hex}" "${ro_b_hex}"; do
    if ! objcopy -I ihex "${f}" -O binary "${TMPD}/${count}.bin"; then
      echo "failed to convert ${f} from hex to bin" >&2
      exit 1
    fi
    verify_ro "${TMPD}/${count}.bin" "${image_type}"
    : $(( count += 1 ))
  done

  if [ "${image_type}" == "prod" ]; then
    extra_param+=' prod'
  fi

  if ! "${EC_ROOT}/util/signer/bs" ${extra_param} elves \
    "${rw_a}" "${rw_b}" > /dev/null;
  then
    echo "Failed invoking ${EC_ROOT}/util/signer/bs ${extra_param} " \
      "elves ${rw_a} ${rw_b}" >&2
    exit 1
  fi

  dd if="${TMPD}/0.bin" of="${RESULT_FILE}" conv=notrunc
  dd if="${TMPD}/1.bin" of="${RESULT_FILE}" seek=262144 bs=1 conv=notrunc

  version="$(usb_updater -b "${RESULT_FILE}" |\
     awk '/^RO_A:/ {gsub(/R[OW]_A:/, ""); print "r" $1 ".w" $2}')"

  if [ -z "${dest_dir}" ]; then
    # Note that this is a global variable
    dest_dir="cr50.${version}"
    if [ ! -d "${dest_dir}" ]; then
      mkdir "${dest_dir}"
    else
      echo "${dest_dir} already exists, will overwrite" >&2
    fi
  elif [ "${dest_dir}" != "cr50.${version}" ]; then
    echo "dev and prod versions mismatch!" >&2
    exit 1
  fi

  cp "${RESULT_FILE}" "${dest_dir}/cr50.bin.${image_type}"
  echo "saved ${image_type} binary in ${dest_dir}/cr50.bin.${image_type}"
}

# Execution starts here ===========================
TMPD="$(mktemp -d /tmp/$(basename $0).XXXXX)"
trap "/bin/rm -rf ${TMPD}" SIGINT SIGTERM EXIT

EC_ROOT="${CROS_WORKON_SRCROOT}/src/platform/ec"
RESULT_FILE="${TMPD}/release.bin"
dest_dir=
IMAGE_SIZE='524288'
export RESULT_FILE

if [ -z "${CROS_WORKON_SRCROOT}" ]; then
 echo "$(basename $0): This script must run inside Chrome OS chroot" >&2
  exit 1
fi

if [ "${#*}" != "6" ]; then
  echo "six parameters are required: "
  echo "<prod RO A>.hex " \
    "<prod RO B>.hex <dev RO A>.hex <dev RO B>.hex <RW.elf> <RW_B.elf>" >&2
  exit 1
fi

dd if=/dev/zero bs="${IMAGE_SIZE}" count=1  2>/dev/null |
  tr \\000 \\377 > "${RESULT_FILE}"
if [ "$(stat -c '%s' "${RESULT_FILE}")" != "${IMAGE_SIZE}" ]; then
  echo "Failed creating ${RESULT_FILE}" >&2
  exit 1
fi

prod_ro_a="${1}"
prod_ro_b="${2}"
dev_ro_a="${3}"
dev_ro_b="${4}"
rw_a="${5}"
rw_b="${6}"

prepare_image 'dev' "${dev_ro_a}" "${dev_ro_b}" "${rw_a}" "${rw_b}"
prepare_image 'prod' "${prod_ro_a}" "${prod_ro_b}" "${rw_a}" "${rw_b}"
tarball="${dest_dir}.tbz2"
tar jcf  "${tarball}" "${dest_dir}"
rm -rf "${dest_dir}"

bcs_path="gs://chromeos-localmirror/distfiles"
echo "SUCCESS!!!!!!"
echo "use the below commands to copy the new image to the BCS"
echo "gsutil cp ${tarball} ${bcs_path}"
echo "gsutil acl ch -u AllUsers:R ${bcs_path}/${tarball}"
