How to release
==============

**Developing**

* First, develop some new features to release!  As you do, make sure to keep the documentation
  up-to-date.

**Testing**

* Run `super-test.sh` on as many platforms as you have available.  Remember that you can easily run
  on any machine available through ssh using `./super-test.sh remote [hostname]`.  Also run in
  Clang mode.  (If you are Kenton and running from Kenton's home machine and network, use
  `./mega-test.py mega-test.cfg` to run on all supported compilers and platforms.)

* Manually test Windows/MSVC -- unfortunately this can't be automated by super-test.sh.

* Manually run the pointer fuzz tests under Valgrind. This will take 40-80 minutes.

      valgrind ./capnp-test -fcapnp/fuzz-test.c++

* Manually run the AFL fuzz tests by running `afl-fuzz.sh`. There are three test cases, and ideally each should run for 24 hours or more.

**Documenting**

* Write a blog post discussing what is new, placing it in doc/_posts.

* Run jekyll locally and review the blog post and docs.

**Releasing**

* Check out the master branch in a fresh directory.  Do NOT use your regular repo, as the release
  script commits changes and if anything goes wrong you'll probably want to trash the whole thing
  without pushing.  DO NOT git clone the repo from an existing local repo -- check it out directly
  from github.  Otherwise, when it pushes its changes back, they'll only be pushed back to your
  local repo.

* Run `./release.sh candidate`.  This creates a new release branch, updates the version number to
  `-rc1`, builds release tarballs, copies them to the current directory, then switches back to the
  master branch and bumps the version number there.  After asking for final confirmation, it will
  upload the tarball to S3 and push all changes back to github.

* Install your release candidates on your local machine, as if you were a user.

* Go to `c++/samples` in the git repo and run `./test.sh`.  It will try to build against your
  installed copy.

* Post the release candidates somewhere public and then send links to the mailing list for people
  to test.  Wait a bit for bug reports.

* If there are any problems, fix them in master and start a new release candidate by running
  `./release.sh candidate <commit>...` from the release branch.  This will cherry-pick the specified
  commits into the release branch and create a new candidate.  Repeat until all problems are fixed.
  Be sure that any such fixes include tests or process changes so that they don't happen again.

* You should now be ready for an official release.  Run `./release.sh final`.  This will remove the
  "-rcN" suffix from the version number, update the version number shown on the downloads page,
  build the final release package, and -- after final confirmation -- upload the binary, push
  changes to git, and publish the new documentation.

* Submit the newly-published blog post to news sites and social media as you see fit.

* If problems are discovered in the release, fix them in master and run
  `./release.sh candidate <commit>...` in the release branch to start a new micro release.  The
  script automatically sees that the current branch's version no longer contains `-rc`, so it starts
  a new branch.  Repeat the rest of the process above.  If you decide to write a blog post (not
  always necessary), do it in the master branch and cherry-pick it.
