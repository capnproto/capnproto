How to release
==============

* Check out the master branch in a fresh directory.  Do NOT use your regular repo, as the release
  script commits changes and if anything goes wrong you'll probably want to trash the whole thing
  without pushing.

* Run `super-test.sh` on as many platforms as you have available.  Remember that you can easily run
  on any machine available through ssh using `./super-test.sh remote [hostname]`.  Also run in
  clang mode.

* Run `./release.sh candidate`.  This creates a new release branch, updates the version number to
  `-rc1`, builds release tarballs, copies them to the current directory, then switches back to the
  master branch and bumps the version number there.

* Install your release candidates on your local machine, as if you were a user.

* Go to `c++/samples` in the git repo and run `./test.sh`.  It will try to build against your
  installed copy.

* Push changes to git for both the master and release branches.

* Post the release candidates somewhere public and then send links to the mailing list for people
  to test.  Wait a bit for bug reports.

* If there are any problems, fix them in master and start a new release candidate by running
  `./release.sh candidate <commit>...` from the release branch.  This will cherry-pick the specified
  commits into the release branch and create a new candidate.  Repeat until all problems are fixed.
  Be sure that any such fixes include tests or process changes so that they don't happen again.

* You should now be ready for an official release.  Run `./release.sh final`.

* Upload the files and update the download links on the web site.

* Write and publish a blog post discussing what is new.

* Submit the blog post to news sites and social media as you see fit.

* If problems are discovered in the release, fix them in master and run
  `./release.sh candidate <commit>...` in the release branch to start a new micro release.  The
  script automatically sees that the current branch's version no longer contains `-rc`, so it starts
  a new branch.  Repeat the rest of the process above.  Blog posts are usually not warranted for
  minor bugfix releases, but the people reporting the bug should of course be told of the fix.
