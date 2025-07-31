# Maintainer: Your Name <your.email@example.com>
pkgname=ss-lib
pkgver=2.0.0
pkgrel=1
pkgdesc="Lightweight signal-slot library for C with focus on embedded systems"
arch=('i686' 'x86_64' 'armv7h' 'aarch64')
url="https://github.com/dardevelin/ss_lib"
license=('MIT')
depends=()
makedepends=('gcc' 'make')
source=("$pkgname-$pkgver.tar.gz::https://github.com/dardevelin/ss_lib/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/ss_lib-$pkgver"
    make
}

check() {
    cd "$srcdir/ss_lib-$pkgver"
    make test
}

package() {
    cd "$srcdir/ss_lib-$pkgver"
    
    # Install library
    install -Dm644 build/libss_lib.a "$pkgdir/usr/lib/libss_lib.a"
    
    # Install headers
    install -Dm644 include/ss_lib.h "$pkgdir/usr/include/ss_lib/ss_lib.h"
    install -Dm644 include/ss_config.h "$pkgdir/usr/include/ss_lib/ss_config.h"
    
    # Install single header version
    ./create_single_header.sh
    install -Dm644 ss_lib_single.h "$pkgdir/usr/include/ss_lib_single.h"
    
    # Install pkg-config file
    install -Dm644 ss_lib.pc "$pkgdir/usr/lib/pkgconfig/ss_lib.pc"
    
    # Install license
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    
    # Install documentation
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 docs/api-reference.md "$pkgdir/usr/share/doc/$pkgname/api-reference.md"
    install -Dm644 docs/getting-started.md "$pkgdir/usr/share/doc/$pkgname/getting-started.md"
}