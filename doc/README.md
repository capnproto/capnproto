# Cap'n Proto Documentation

This directory contains the "source code" for the Cap'n Proto web site.

The site is built with [Jekyll](http://jekyllrb.com/).  Start by installing it.

Be sure to follow the instructions to install Pygments as well.

Next, install the custom Pygments syntax highlighter:

    cd _plugins
    python capnp_lexer.py install
    cd ..

Now you can launch a local server:

    jekyll --server --auto --pygments

Edit, test, commit.

If you have permission, after you've pushed your changes back to github, you can make your changes live by running:

    ./push-site.sh

Otherwise, send a pull request and let someone else actually push the new site.
