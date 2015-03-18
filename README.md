random hints:

place repo in TOPSRCDIR/extensions

modify .mozconfig
ac_add_options --enable-extensions=default,network_tests_012015

set extensions.sdk.console.logLevel to "debug"

create an xpi as a zip of the addons subdir and load that in
firefox. if you wish to develop the addon, after install and shutdown,
delete the xpi in your profile/extensions and link to the addon
directory in the repo using the same name (but without the xpi
extension). The meat of the addon, such as it is, can be found in
resources/network-test/lib/main.js



