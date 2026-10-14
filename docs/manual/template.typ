// BRICK Pandoc/Typst template. Content comes from the numbered Markdown files.
#let brick-ink = rgb("20242b")
#let brick-muted = rgb("66707d")
#let brick-accent = rgb("d66a32")
#let brick-panel = rgb("f3f4f6")
#let brick-rule = rgb("d5d9df")

#set document(
  title: "$title$",
  author: "$author$",
)
#set page(
  paper: "a4",
  margin: (top: 22mm, bottom: 20mm, left: 23mm, right: 20mm),
)
#set text(font: ("Arial", "Liberation Sans"), size: 10.5pt, fill: brick-ink)
#set par(justify: true, leading: 0.68em)
#set list(indent: 1.25em, body-indent: 0.55em)
#set enum(indent: 1.25em, body-indent: 0.55em)
#set table(
  inset: (x: 6pt, y: 4pt),
  stroke: brick-rule,
  fill: (_, row) => if row == 0 { rgb("e8eaee") } else { none },
)
#set heading(numbering: "1.1")
#show heading.where(level: 1): set text(size: 20pt, weight: "bold", fill: brick-ink)
#show heading.where(level: 2): set text(size: 14pt, weight: "bold", fill: brick-accent)
#show heading.where(level: 3): set text(size: 11.5pt, weight: "bold", fill: brick-ink)
#show heading.where(level: 1): it => {
  pagebreak(weak: true)
  block(above: 0pt, below: 14pt)[#it]
}
#show link: set text(fill: rgb("255d86"))
#show quote: it => block(
  width: 100%,
  inset: 10pt,
  radius: 2pt,
  fill: brick-panel,
  stroke: (left: 2.5pt + brick-accent),
  breakable: true,
)[#it]
#show raw.where(block: true): it => block(
  width: 100%,
  inset: 9pt,
  radius: 2pt,
  fill: rgb("f6f7f8"),
  stroke: 0.5pt + brick-rule,
)[#set text(font: ("Cascadia Mono", "Consolas", "Liberation Mono"), size: 8.8pt); #it]
#show figure: it => align(center, block(width: 88%, breakable: false)[#it])
#show figure.caption: set text(size: 9pt, style: "italic", fill: brick-muted)

// Cover: deliberately separate from the running header/footer.
#set page(header: none, footer: none)
#v(28mm)
#align(center)[
  #rect(width: 18mm, height: 2.5mm, fill: brick-accent, stroke: none)
  #v(16mm)
  #text(size: 38pt, weight: "bold", tracking: 0.08em)[$title$]
  #v(5mm)
  #text(size: 18pt, fill: brick-muted)[$subtitle$]
  #v(24mm)
  #text(size: 11pt, weight: "bold")[Manual version $manual_version$]
  $if(firmware_version)$
  #v(2mm)
  #text(size: 10pt, fill: brick-muted)[Firmware $firmware_version$]
  $endif$
  $if(author)$
  #v(8mm)
  #text(size: 10pt)[$author$]
  $endif$
  #v(1fr)
  #text(size: 9pt, fill: brick-muted)[Generated #datetime.today().display("[day]/[month]/[year]")]
]
#pagebreak()
#counter(page).update(1)

// Running pages.
#set page(
  header: context {
    set text(size: 8pt, fill: brick-muted)
    grid(columns: (1fr, auto), [BRICK — User Manual], [v$manual_version$])
    v(2pt)
    line(length: 100%, stroke: 0.4pt + brick-rule)
  },
  footer: context {
    line(length: 100%, stroke: 0.4pt + brick-rule)
    v(3pt)
    set text(size: 8pt, fill: brick-muted)
    grid(columns: (1fr, auto), [BRICK], [#counter(page).display("1")])
  },
)

#heading(outlined: false, numbering: none)[Contents]
#outline(title: none, depth: 3, indent: auto)
#pagebreak()

$body$
