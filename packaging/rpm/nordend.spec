# Nordend's RPM spec for Fedora. Build it with:
#   spectool -g -C ~/rpmbuild/SOURCES packaging/rpm/nordend.spec
#   sudo dnf builddep packaging/rpm/nordend.spec
#   rpmbuild -ba packaging/rpm/nordend.spec
# or in a COPR project (Fedora's build service). --without check skips the tests.
# .github/workflows/rpm.yml builds it from every commit, on the Fedora releases it lists.
#
# Looked after by 0rgl (https://github.com/0rgl): for questions or problems with the RPM, open
# an issue and mention @0rgl.

%bcond check 1

Name:           nordend
Version:        0.9.5
Release:        1%{?dist}
Summary:        Native Wayland file manager

# MIT: Nordend, fzy, Tabler Icons, Noctalia palette; MIT OR Apache-2.0: wuffs
License:        MIT AND (MIT OR Apache-2.0)
URL:            https://github.com/pankaspe/nordend-fm
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

# See THIRD_PARTY.md; not packaged in Fedora, no upstream switch for system copies.
Provides:       bundled(wuffs) = 0.4.0~alpha.12
Provides:       bundled(fzy) = 1.1
Provides:       bundled(tabler-icons-fonts) = 3.50.0

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  pkgconfig
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(wayland-cursor)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(harfbuzz)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(tomlplusplus)
BuildRequires:  pkgconfig(libwebp)
BuildRequires:  pkgconfig(libwebpdemux)
BuildRequires:  pkgconfig(libjxl)
BuildRequires:  pkgconfig(zlib)
BuildRequires:  pkgconfig(sdbus-c++) >= 2
%if %{with check}
BuildRequires:  cmake(doctest)
BuildRequires:  pkgconfig(shared-mime-info)
BuildRequires:  dbus-daemon
BuildRequires:  mesa-dri-drivers
BuildRequires:  mesa-libEGL
BuildRequires:  dejavu-sans-fonts
# renderer_runs_test draws with the monospace typeface
BuildRequires:  dejavu-sans-mono-fonts
BuildRequires:  adwaita-cursor-theme
%endif

Requires:       shared-mime-info
Requires:       hicolor-icon-theme
Recommends:     udisks2
Recommends:     xdg-desktop-portal

%description
Nordend is a native file manager for Wayland, written in C++20 on Wayland and
OpenGL ES, without GTK or Qt. Previews are decoded by a sandboxed helper
process.

The package also ships nordend-portal, a file chooser backend for
xdg-desktop-portal (enabled per user with nordend --set-file-chooser) that also
serves org.freedesktop.FileManager1 once a user runs
nordend --set-file-manager.

%prep
%autosetup -n nordend-fm-%{version} -p1
cp -p third_party/wuffs/LICENSE-MIT wuffs-LICENSE-MIT
cp -p third_party/wuffs/LICENSE-APACHE wuffs-LICENSE-APACHE
cp -p third_party/fzy/LICENSE fzy-LICENSE
cp -p third_party/tabler-icons/LICENSE tabler-icons-LICENSE

%conf
%cmake -G Ninja \
    -DBUILD_SHARED_LIBS=OFF \
    -DNORDEND_WERROR=OFF \
    -DNORDEND_INSTALL_SETUP=OFF \
    -DNORDEND_BUILD_TESTS=%{?with_check:ON}%{!?with_check:OFF}

%build
%if %{with check}
%cmake_build
%else
%cmake_build --target nordend nordend-preview nordend-portal
%endif

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/nordend.desktop
%if %{with check}
test -x %{_vpath_builddir}/tests/nordend-tests
%ctest
%endif

%files
%license LICENSE
%license wuffs-LICENSE-MIT wuffs-LICENSE-APACHE fzy-LICENSE tabler-icons-LICENSE
%doc README.md CHANGELOG.md THIRD_PARTY.md docs/config.example.toml
%{_bindir}/nordend
%{_libexecdir}/nordend/
%{_datadir}/nordend/
%{_datadir}/applications/nordend.desktop
%{_datadir}/icons/hicolor/scalable/apps/nordend.svg
%dir %{_datadir}/xdg-desktop-portal
%dir %{_datadir}/xdg-desktop-portal/portals
%{_datadir}/xdg-desktop-portal/portals/nordend.portal
%dir %{_datadir}/dbus-1
%dir %{_datadir}/dbus-1/services
%{_datadir}/dbus-1/services/org.freedesktop.impl.portal.desktop.nordend.service
%{bash_completions_dir}/nordend.bash
%{fish_completions_dir}/nordend.fish
%{zsh_completions_dir}/_nordend

%changelog
* Fri Nov 20 2026 pankaspe <7622507+pankaspe@users.noreply.github.com> - 0.9.5-1
- Scripts of your own in the context menu; Ctrl+Shift+I and Ctrl+Shift+T; completions for
  bash, zsh and fish; access times left alone; a disk not powered off says why

* Fri Nov 20 2026 pankaspe <7622507+pankaspe@users.noreply.github.com> - 0.9.1-1
- AppImages run; Find Files ranks names first and takes terms; text fields copy, cut and
  select; pasting into a folder reached through a link

* Mon Nov 16 2026 pankaspe <7622507+pankaspe@users.noreply.github.com> - 0.9.0-1
- Archives: zip, tar and tar.gz browsed, extracted and made; .iso images mounted

* Sun Nov 15 2026 0rgl <30941756+0rgl@users.noreply.github.com> - 0.8.1-1
- First RPM packaging of Nordend
