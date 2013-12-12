function initSidebar() {
  var filename = document.location.pathname.slice("/capnproto".length);

  if (filename.slice(0, 5) == "/next") {
    filename = filename.slice(5);
  }

  if (filename == "/") {
    filename = "/index.html";
  } else if (filename.slice(0, 6) == "/news/") {
    filename = "/news/";
  }

  var menu = document.getElementById("menu");
  var setMenuLayout = function() {
    if (window.innerWidth < 900) {
      document.body.className = "narrow";
      menu.className = "";
    } else {
      if (document.body.clientWidth < 1340) {
        document.body.className = "normal";
      } else {
        document.body.className = "wide";
      }

      var y = (window.pageYOffset !== undefined) ? window.pageYOffset :
        (document.documentElement || document.body.parentNode || document.body).scrollTop;

      if (y < 444 || window.innerHeight < menu.clientHeight + 100) {
        menu.className = "";
      } else {
        menu.className = "floating";
      }
    }
  };
  setMenuLayout();
  window.onresize = setMenuLayout;
  window.onscroll = setMenuLayout;

  var items = menu.getElementsByTagName("li");
  var toc = null;
  for (var i = 0; i < items.length; i++) {
    var link = items[i].getElementsByTagName("a")[0];
    var href = link.href;
    if (href.lastIndexOf(filename) >= 0) {
      var parent = link.parentNode;
      var p = document.createElement("p");

      while (link.childNodes.length > 0) {
        var child = link.childNodes[0];
        link.removeChild(child);
        p.appendChild(child);
      }
      parent.removeChild(link);
      p.onclick = (function(url) {
        return function(event) {
          window.location.href = url;
          event.stopPropagation();
        }
      })(href + "#");
      parent.appendChild(p);
      items[i].className = "selected";
      toc = document.createElement("ul");
      toc.id = "toc";
      items[i].appendChild(toc);
    } else {
      items[i].onclick = (function(url) {
        return function(event) {
          window.location.href = url;
          event.stopPropagation();
        }
      })(href);
    }
  }

  document.getElementById("main_content").style.minHeight = menu.clientHeight + 100 + "px";

  return toc;
}

function setupSidebar() {
  var isNews = document.location.pathname.slice("/capnproto".length).slice(0, 6) == "/news/";

  var toc = initSidebar();
  if (toc) {
    var content = document.getElementById("main_content").childNodes;
    var headings = [];

    for (var i = 0; i < content.length; i++) {
      if (content[i].tagName == "H2" ||
          (!isNews && (content[i].tagName == "H3" || content[i].tagName == "H4"))) {
        headings.push(content[i]);
      }
    }

    var levels = [toc];
    for (var i in headings) {
      var hl = headings[i].tagName.slice(1) - 1;
      while (hl > levels.length) {
        var parent = levels[levels.length - 1];
        var item = parent.childNodes[parent.childNodes.length - 1];
        var sublist = document.createElement("ul");
        item.appendChild(sublist);
        levels.push(sublist);
      }
      while (hl < levels.length) {
        levels.pop();
      }

      var parent = levels[levels.length - 1];
      var item = document.createElement("li");
      var p = document.createElement("p");
      var link = document.createElement("a");
      link.appendChild(document.createTextNode(headings[i].innerText || headings[i].textContent));
      var hlinks = headings[i].getElementsByTagName("a");
      if (hlinks.length == 1) {
        link.href = hlinks[0].href;
      } else {
        link.href = "#" + headings[i].id;
      }
      p.appendChild(link);
      p.onclick = (function(url) {
        return function(event) {
          window.location.href = url;
          event.stopPropagation();
        }
      })(link.href);
      item.appendChild(p);
      parent.appendChild(item);
    }
  }
}

function setupNewsSidebar(items) {
  var toc = initSidebar();
  if (toc) {
    for (var i in items) {
      var item = document.createElement("li");
      var p = document.createElement("p");
      var link = document.createElement("a");
      link.appendChild(document.createTextNode(items[i].title));
      link.href = items[i].url;
      p.appendChild(link);
      p.onclick = (function(url) {
        return function(event) {
          window.location.href = url;
          event.stopPropagation();
        }
      })(link.href);
      item.appendChild(p);
      toc.appendChild(item);
    }
  }
}
