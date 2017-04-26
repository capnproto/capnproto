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
