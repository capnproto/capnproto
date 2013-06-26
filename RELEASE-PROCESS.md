How to release
==============

* Run `super-test.sh` on as many platforms as you have available.  Remember that you can easily run
  on any machine available through ssh using `./super-test.sh remote [hostname]`.  Also run in
  clang mode.

* Create a new release branch, named release-VERSION, where VERSION is the current version number
  except with `-dev` replaced by `.0`.  E.g. 1.1-dev becomes 1.1.0.

* In the master branch, bump the minor version number.

* In the release branch, change the version numbers in both `c++/configure.ac` and
  `compiler/capnproto-compiler.cabal`, replacing `-dev` with `.0-rc1`.  So, e.g., 1.1-dev becomes
  1.1.0-rc1.

* Under the `compiler` directory, run `cabal sdist` to build a release candidate tarball.

* Under the `c++` directory, use `make distcheck` to build a release candidate tarball.

* Install your release candidates on your local machine.

* Go to `c++/samples` in the git repo and run `./test.sh`.  It will try to build against your
  installed copy.

* Post the release candidates somewhere public and then send links to the mailing list for people
  to test.  Wait a bit for bug reports.

* If there are any problems, fix them and start a new release candidate `-rc2`, repeating the
  instructions above, and so on until the problem is fixed.  Make sure to cherry-pick any changes
  from the release branch back into the mainline.

* You should now be ready for an official release.  Remove the `-rcN` suffix from the verison
  number and rebuild the release packages one last time.  Post these publicly and update the links
  on the site.

* Write and publish a blog post discussing what is new.

* Submit the blog post to Hacker News and other places.

* If problems are discovered in the release, make a new release branch forked from the current one
  (not from master) with the micro version incremented by one and fix the problems there.  Repeat
  the release candidate process if the changes are complicated, or skip it and just publish if the
  release is simple and obvious.  Blog posts are usually not warranted for minor bugfix releases,
  but the people reporting the bug should of course be told of the fix.  Be sure to cherry-pick the
  fix back into the mainline.  If at all possible, include a test with your fix so that it doesn't
  happen again.  The test may be a unit test, an extension of `super-test.sh`, or a new step in the
  release process.
