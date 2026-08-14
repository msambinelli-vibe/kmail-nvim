# Maintainer: KMail Vim Navigation contributors
# SPDX-License-Identifier: GPL-2.0-or-later

pkgname=kmail-vim-navigation
pkgver=1.1.0
pkgrel=1
pkgdesc='Vim-style navigation and deferred message actions for KMail'
arch=('x86_64')
url='https://github.com/msambinelli/kmail-nvim'
license=('GPL-2.0-or-later')
depends=(
  'akonadi'
  'akonadi-mime'
  'kconfig'
  'kcoreaddons'
  'kmail'
  'kxmlgui'
  'messagelib'
  'pimcommon'
  'qt6-base'
)
makedepends=(
  'cmake'
  'extra-cmake-modules'
)
source=("kmail-nvim-${pkgver}.tar.gz::${url}/archive/refs/tags/v${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
  cmake \
    -B build \
    -S "kmail-nvim-${pkgver}" \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=ON
  cmake --build build
}

check() {
  ctest --test-dir build --output-on-failure
}

package() {
  DESTDIR="${pkgdir}" cmake --install build
}

