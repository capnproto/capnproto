# Cap'n Proto Documentation

This directory contains the "source code" for the Cap'n Proto web site.

The site is built with [Jekyll](http://jekyllrb.com/), which depends on Ruby. 
Start by installing ruby1.9.1-dev. On Debian-based operating systems:

    sudo apt-get install ruby-dev

Then install Jekyll:

    sudo gem install jekyll pygments.rb

Now install Pygments and SetupTools to be able to install the CapnProto lexer.
On Debian based operating systems:

    sudo apt-get install python-pygments python-setuptools

Next, install the custom Pygments syntax highlighter:

    cd _plugins
    sudo python capnp_lexer.py install
    cd ..

Now you can launch a local server:

    jekyll serve --watch

Edit, test, commit.

If you have permission, after you've pushed your changes back to github, you can make your changes live by running:

    ./push-site.sh

Otherwise, send a pull request and let someone else actually push the new site.

# Package Managers

You can download and install capnproto using the [vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:

    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ./vcpkg install capnproto

The capnproto port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.
