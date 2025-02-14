part = "bottom"; // ['full','top','bottom']

module board() {
  translate([0, 0, (max_h + 2 + 2) / 2])
    cube([93 - 2, 72 - 2, max_h], true);
  color("green") translate([0, 0, 1]) cube([93, 72, 2], true);
  translate([93 / 2 - 17, -50, 8]) cube([12, 100, 12], true);
  hull() {
    translate([93 / -2 + 29, 50, 2 + 3.25]) rotate([90, 0, 0])
        cylinder(d=7, h=100, center=true);
    translate([93 / -2 + 29, 50, 2 + 1]) rotate([90, 0, 0])
        cylinder(d=7, h=100, center=true);
  }
  difference() {
    translate([0, 0, -1.5]) cube([93, 72, 3], true); for (x = [.5, -.5], y = [.5, -.5])
      translate([x * 85, y * 64, -10]) hull() {
          cylinder(d=7, h=20, true);
          translate([10 * x, 0, 0]) cylinder(d=7, h=20, true);
          translate([0, 10 * y, 0]) cylinder(d=7, h=20, true);
        }
  }
}

// board();

module top() {
  difference() {
    hull()for (x = [.5, -.5], y = [.5, -.5])
      translate([x * 93, y * 72, 0]) cylinder(d=3, h=max_h + 3);
    board();
    bottom();

    for (y = [-33, -28, -23, -18, 18, 23, 28, 33]) {
      translate([0, y, 0])for (X = [-1.5, -.5, .5, 1.5])
        translate([X * 23, 0, 0]) hull()for (x = [-10, 10])
            translate([x, 0, 0]) cylinder(d=2, h=1000);
      for (x = [-.5, .5])
        translate([96.75 * x, y, 0]) cube([2, 2, 200], true);
      // translate([0,y,max_h+3.75])cube([200,2,2],true);
    }
  }
}

if (part != "top") {
  union() difference() {
      bottom();
      bottom_logo(); for (y = [-33, -28, -23, -18, 18, 23, 28, 33]) {
        translate([0, y, 0])for (X = [-1.5, -.5, .5, 1.5])
          translate([X * 23, 0, 0]) hull()for (x = [-10, 10])
              translate([x, 0, 0]) cylinder(d=2, h=1000);
        for (x = [-.5, .5]) translate([96.75 * x, y, 0]) cube([2, 2, 200], true);
        //translate([0,y,-4.75])cube([200,2,2],true);
      }
    }

  color("red") union() bottom_logo();
}

if (part != "bottom") {
  union() {
    difference() {
      top();
      top_logo();
    }
    difference() {
      union()for (x = [-.5, .5], y = [.5, -.5])
        hull() {
          translate([x * 85, y * 64, 3]) {
            cylinder(d=6, h=10);
            translate([3 * x, 3 * y, 5]) cube([6, 6, 10], true);
            translate([6 * x, 6 * y, 15]) cylinder(d=0.1, h=2);
          }
        }
      for (x = [.5, -.5], y = [.5, -.5])
        translate([x * 85, y * 64, 3]) { cylinder(d=2.6, h=10); }
    }
  }

  color("red") union() top_logo();
}

module bottom_logo() {
  translate([46.5, -15, -4]) mirror([1, 0, 0]) linear_extrude(0.5)
        scale(.092) import(file="usbsid-white.dxf");
}

module top_logo() {
  translate([-46, -15, max_h + 2.5]) linear_extrude(0.5) scale(.092)
        import(file="usbsid-white.dxf");
}

module bottom() {
  difference() {
    hull()for (x = [.5, -.5], y = [.5, -.5])
      translate([x * 93, y * 72, -4]) cylinder(d=3, h=7);
    board();
    translate([0, 0, 1]) board();

    for (x = [.5, -.5], y = [.5, -.5])
      translate([x * 85, y * 64, -10]) {
        cylinder(d=3.5, h=1000, center=true);
        translate([0, 0, 6.5]) cylinder(d=6, h=1, center=true);
        translate([0, 0, 6.5 + 0.5 + 0.3 / 2])
          cube([3.5, 6, 0.3], center=true);
      }
  }
}

$fn = $preview ? 32 : 128;

max_h = 25;
