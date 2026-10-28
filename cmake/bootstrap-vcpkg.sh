#!/usr/bin/env bash
set -euo pipefail

readonly vcpkg_commit="74e6536215718009aae747d86d84b78376bf9e09"
readonly vcpkg_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.vcpkg/vcpkg"

if [[ -d "${vcpkg_dir}/.git" ]]; then
  actual_commit="$(git -C "${vcpkg_dir}" rev-parse HEAD)"
  if [[ "${actual_commit}" != "${vcpkg_commit}" ]]; then
    echo "vcpkg checkout is ${actual_commit}; expected ${vcpkg_commit}" >&2
    exit 1
  fi
else
  mkdir -p "$(dirname "${vcpkg_dir}")"
  git clone https://github.com/microsoft/vcpkg.git "${vcpkg_dir}"
  git -C "${vcpkg_dir}" checkout --detach "${vcpkg_commit}"
fi

"${vcpkg_dir}/bootstrap-vcpkg.sh" -disableMetrics
