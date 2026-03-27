#!/usr/bin/env bb

;
; USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
; for interfacing one or two MOS SID chips and/or hardware SID emulators over
; (WEB)USB with your computer, phone or ASID supporting player
;
; genlist.bb
; This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
; File author: LouD
;
; Copyright (c) 2024-2026 LouD
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, version 2.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program. If not, see <http://www.gnu.org/licenses/>.
;

; This file contains a Babashka script to create sidfilelist.txt
; from sidfiles in this directory and recursive directories

(require '[babashka.fs :as fs]
         '[clojure.java.io :as io])

(def all-files (mapv str (fs/glob "." "**{.sid}")))
(def psid (sort (filter #(.contains % "psid/") all-files)))
(def indirs (sort (filter #(.contains % "/") all-files)))
(def dualsid (sort (filter #(.contains % "2SID") psid)))
(def triplesid (sort (filter #(.contains % "3SID") psid)))
(def quadsid (sort (filter #(.contains % "4SID") psid)))
(def singlesid (sort (remove (set (concat dualsid triplesid quadsid)) psid)))
(def filename "sidfilelist.txt")
(if (fs/exists? filename)
  (fs/move filename (str filename ".old")))
(with-open [wr (io/writer filename)]
  (.write wr (with-out-str (println "<-- Single SID -->")))
  (doseq [file singlesid] (.write wr (with-out-str (println file))))
  (.write wr (with-out-str (println "<-- Dual SID -->")))
  (doseq [file dualsid] (.write wr (with-out-str (println file))))
  (.write wr (with-out-str (println "<-- Triple SID -->")))
  (doseq [file triplesid] (.write wr (with-out-str (println file))))
  (.write wr (with-out-str (println "<-- Quad SID -->")))
  (doseq [file quadsid] (.write wr (with-out-str (println file))))
  (.write wr (with-out-str (println "<-- CRSID -->")))
  (doseq [file indirs] (when (.contains file "crsid") (.write wr (with-out-str (println file)))))
  (.write wr (with-out-str (println "<-- DEMO's -->")))
  (doseq [file indirs] (when (.contains file "demos") (.write wr (with-out-str (println file)))))
  (.write wr (with-out-str (println "<-- Digitunes -->")))
  (doseq [file indirs] (when (.contains file "rsid") (.write wr (with-out-str (println file)))))
  (.write wr (with-out-str (println "<-- FMOpl -->")))
  (doseq [file indirs] (when (.contains file "fmopl") (.write wr (with-out-str (println file)))))
  (.write wr (with-out-str (println "<-- TEST -->")))
  (doseq [file indirs] (when (.contains file "test") (.write wr (with-out-str (println file))))))
