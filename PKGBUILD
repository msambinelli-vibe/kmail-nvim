# Maintainer: KMail Vim Navigation contributors
# SPDX-License-Identifier: GPL-2.0-or-later

pkgname=kmail-vim-navigation-git
pkgver=1.1.0.r1.gbae86f0
pkgrel=1
pkgdesc='Vim-style navigation and deferred message actions for KMail (master branch)'
arch=('x86_64')
url='https://github.com/msambinelli-vibe/kmail-nvim'
license=('GPL-2.0-or-later')
provides=('kmail-vim-navigation')
conflicts=('kmail-vim-navigation')
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
  'git'
)
source=("kmail-nvim::git+${url}.git#branch=master")
sha256sums=('SKIP')

pkgver() {
  cd kmail-nvim
  printf '1.1.0.r%s.g%s' "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
}

build() {
  cmake \
    -B build \
    -S kmail-nvim \
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
