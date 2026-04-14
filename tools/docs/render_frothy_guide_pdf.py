#!/usr/bin/env python3
"""Render the Frothy guide Markdown into a polished PDF.

This renderer intentionally supports a narrow, predictable Markdown subset:

- headings (`#`, `##`, `###`)
- paragraphs
- flat bullet and numbered lists
- fenced code blocks

The Markdown guide is the source of truth. This script adds layout, running
headers, a title page, a table of contents, and styled callout boxes for the
guide's recurring teaching sections.
"""

from __future__ import annotations

import argparse
import html
import re
import textwrap
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

try:
    from reportlab.lib import colors
    from reportlab.lib.enums import TA_CENTER, TA_RIGHT
    from reportlab.lib.pagesizes import letter
    from reportlab.lib.styles import ParagraphStyle, StyleSheet1, getSampleStyleSheet
    from reportlab.lib.units import inch
    from reportlab.pdfgen import canvas as canvas_mod
    from reportlab.platypus import (
        BaseDocTemplate,
        Frame,
        ListFlowable,
        ListItem,
        PageBreak,
        PageTemplate,
        Paragraph,
        Preformatted,
        Spacer,
        Table,
        TableStyle,
    )
    from reportlab.platypus.tableofcontents import TableOfContents
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing PDF dependency: reportlab.\n"
        "Install it with `python3 -m pip install reportlab` and rerun."
    ) from exc


GUIDE_TITLE = "Frothy From The Ground Up"
GUIDE_SUBTITLE = "A maintainer's manual for the Frothy language and codebase"

CALLOUT_STYLES = {
    "Code Walk": colors.HexColor("#E8F1FB"),
    "Worked Example": colors.HexColor("#EEF8EA"),
    "What to remember": colors.HexColor("#FFF5D8"),
    "Common confusions": colors.HexColor("#FBEAEA"),
    "Invariants": colors.HexColor("#F2EDF9"),
    "Hands-on walkthrough": colors.HexColor("#EAF6F7"),
    "What can go wrong": colors.HexColor("#FDEFE2"),
}

INLINE_CODE_RE = re.compile(r"`([^`]+)`")
BOLD_RE = re.compile(r"\*\*([^*]+)\*\*")
ITALIC_RE = re.compile(r"\*([^*]+)\*")


@dataclass
class Block:
    kind: str
    text: str | None = None
    level: int | None = None
    items: list[str] | None = None
    language: str | None = None


class ManualDocTemplate(BaseDocTemplate):
    def __init__(self, filename: str, styles: StyleSheet1, **kwargs) -> None:
        self.styles = styles
        self.heading_map: dict[int, str] = {}
        self._toc = TableOfContents()
        self._toc.levelStyles = [
            ParagraphStyle(
                "TOCLevel1",
                fontName="Times-Bold",
                fontSize=11,
                leading=14,
                leftIndent=0,
                firstLineIndent=0,
                spaceBefore=4,
                textColor=colors.HexColor("#17324D"),
            ),
            ParagraphStyle(
                "TOCLevel2",
                fontName="Times-Roman",
                fontSize=10,
                leading=12,
                leftIndent=18,
                firstLineIndent=-8,
                spaceBefore=2,
            ),
        ]
        super().__init__(filename, pagesize=letter, **kwargs)

        frame = Frame(
            0.85 * inch,
            0.72 * inch,
            6.8 * inch,
            9.2 * inch,
            leftPadding=0,
            rightPadding=0,
            topPadding=0,
            bottomPadding=0,
            id="normal",
        )
        self.addPageTemplates(
            [
                PageTemplate(id="main", frames=[frame], onPage=self._draw_header_footer),
            ]
        )

    @property
    def toc(self) -> TableOfContents:
        return self._toc

    def afterFlowable(self, flowable) -> None:  # noqa: N802
        level = getattr(flowable, "_heading_level", None)
        text = getattr(flowable, "_heading_text", None)
        if level is None or text is None:
            return
        page = self.canv.getPageNumber()
        if level <= 2:
            self.notify("TOCEntry", (level - 1, text, page))
            self.heading_map[page] = text

    def section_title_for_page(self, page: int) -> str:
        if not self.heading_map:
            return GUIDE_TITLE
        pages = sorted(self.heading_map)
        idx = bisect_right(pages, page) - 1
        if idx < 0:
            return GUIDE_TITLE
        return self.heading_map[pages[idx]]

    def _draw_header_footer(self, canv, doc) -> None:
        page = canv.getPageNumber()
        if page == 1:
            return
        canv.saveState()
        canv.setStrokeColor(colors.HexColor("#D6DDE5"))
        canv.line(0.85 * inch, 10.25 * inch, 7.65 * inch, 10.25 * inch)
        canv.setFont("Times-Roman", 9)
        canv.setFillColor(colors.HexColor("#45617B"))
        canv.drawString(0.85 * inch, 10.35 * inch, GUIDE_TITLE)
        if page <= 3:
            section = "Contents"
        else:
            section = textwrap.shorten(
                self.section_title_for_page(page), width=52, placeholder="..."
            )
        canv.drawRightString(7.65 * inch, 10.35 * inch, section)
        canv.line(0.85 * inch, 0.58 * inch, 7.65 * inch, 0.58 * inch)
        canv.setFillColor(colors.HexColor("#526477"))
        canv.drawCentredString(4.25 * inch, 0.38 * inch, str(page))
        canv.restoreState()


class NumberedCanvas(canvas_mod.Canvas):
    """Two-pass canvas so headers can use the final section map."""

    def __init__(self, *args, section_map_ref: dict[int, str] | None = None, **kwargs):
        self._saved_page_states = []
        self._section_map_ref = section_map_ref if section_map_ref is not None else {}
        super().__init__(*args, **kwargs)

    def showPage(self):  # noqa: N802
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):  # noqa: N802
        page_count = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            super().showPage()
        self._doc._page_count = page_count  # type: ignore[attr-defined]
        super().save()


def make_styles() -> StyleSheet1:
    styles = getSampleStyleSheet()
    styles.add(
        ParagraphStyle(
            "ManualBody",
            parent=styles["BodyText"],
            fontName="Times-Roman",
            fontSize=10.5,
            leading=14,
            spaceAfter=8,
            textColor=colors.HexColor("#1D2733"),
        )
    )
    styles.add(
        ParagraphStyle(
            "ManualH1",
            parent=styles["Heading1"],
            fontName="Times-Bold",
            fontSize=22,
            leading=26,
            spaceBefore=12,
            spaceAfter=12,
            textColor=colors.HexColor("#17324D"),
        )
    )
    styles.add(
        ParagraphStyle(
            "ManualH2",
            parent=styles["Heading2"],
            fontName="Times-Bold",
            fontSize=16,
            leading=20,
            spaceBefore=16,
            spaceAfter=8,
            textColor=colors.HexColor("#17324D"),
        )
    )
    styles.add(
        ParagraphStyle(
            "ManualH3",
            parent=styles["Heading3"],
            fontName="Times-BoldItalic",
            fontSize=12,
            leading=15,
            spaceBefore=10,
            spaceAfter=6,
            textColor=colors.HexColor("#2D4A63"),
        )
    )
    styles.add(
        ParagraphStyle(
            "TitlePageTitle",
            parent=styles["Title"],
            fontName="Times-Bold",
            fontSize=28,
            leading=34,
            alignment=TA_CENTER,
            textColor=colors.HexColor("#17324D"),
            spaceAfter=18,
        )
    )
    styles.add(
        ParagraphStyle(
            "TitlePageSubtitle",
            parent=styles["BodyText"],
            fontName="Times-Italic",
            fontSize=14,
            leading=18,
            alignment=TA_CENTER,
            textColor=colors.HexColor("#4D657A"),
            spaceAfter=18,
        )
    )
    styles.add(
        ParagraphStyle(
            "TOCHeading",
            parent=styles["Heading2"],
            fontName="Times-Bold",
            fontSize=18,
            leading=22,
            spaceAfter=10,
            textColor=colors.HexColor("#17324D"),
        )
    )
    styles.add(
        ParagraphStyle(
            "ListBody",
            parent=styles["ManualBody"],
            leftIndent=12,
            firstLineIndent=0,
            spaceBefore=0,
            spaceAfter=3,
        )
    )
    styles.add(
        ParagraphStyle(
            "CalloutTitle",
            parent=styles["Heading3"],
            fontName="Times-Bold",
            fontSize=12,
            leading=15,
            textColor=colors.HexColor("#17324D"),
            spaceAfter=5,
        )
    )
    styles.add(
        ParagraphStyle(
            "CalloutBody",
            parent=styles["ManualBody"],
            spaceAfter=6,
        )
    )
    return styles


def parse_markdown(text: str) -> list[Block]:
    lines = text.splitlines()
    blocks: list[Block] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            i += 1
            continue

        if stripped.startswith("```"):
            language = stripped[3:].strip() or None
            code_lines = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code_lines.append(lines[i].rstrip("\n"))
                i += 1
            blocks.append(Block(kind="code", text="\n".join(code_lines), language=language))
            i += 1
            continue

        if stripped.startswith("#"):
            level = len(stripped) - len(stripped.lstrip("#"))
            text_value = stripped[level:].strip()
            blocks.append(Block(kind="heading", text=text_value, level=level))
            i += 1
            continue

        if stripped.startswith("- "):
            items = []
            while i < len(lines) and lines[i].strip().startswith("- "):
                items.append(lines[i].strip()[2:].strip())
                i += 1
            blocks.append(Block(kind="ulist", items=items))
            continue

        if re.match(r"^\d+\.\s", stripped):
            items = []
            while i < len(lines) and re.match(r"^\d+\.\s", lines[i].strip()):
                items.append(re.sub(r"^\d+\.\s*", "", lines[i].strip()))
                i += 1
            blocks.append(Block(kind="olist", items=items))
            continue

        para_lines = [stripped]
        i += 1
        while i < len(lines):
            nxt = lines[i].strip()
            if not nxt or nxt.startswith("```") or nxt.startswith("#"):
                break
            if nxt.startswith("- ") or re.match(r"^\d+\.\s", nxt):
                break
            para_lines.append(nxt)
            i += 1
        blocks.append(Block(kind="paragraph", text=" ".join(para_lines)))
    return blocks


def inline_markup(text: str) -> str:
    escaped = html.escape(text)
    escaped = INLINE_CODE_RE.sub(
        lambda m: f'<font name="Courier">{html.escape(m.group(1))}</font>',
        escaped,
    )
    escaped = BOLD_RE.sub(lambda m: f"<b>{m.group(1)}</b>", escaped)
    escaped = ITALIC_RE.sub(lambda m: f"<i>{m.group(1)}</i>", escaped)
    return escaped


def heading_para(text: str, level: int, styles: StyleSheet1) -> Paragraph:
    style_name = {1: "ManualH1", 2: "ManualH2", 3: "ManualH3"}.get(level, "ManualH3")
    para = Paragraph(inline_markup(text), styles[style_name])
    para._heading_level = level
    para._heading_text = text
    return para


def code_block(text: str) -> Table:
    pre = Preformatted(
        text.rstrip(),
        ParagraphStyle(
            "CodeBlock",
            fontName="Courier",
            fontSize=8.3,
            leading=10.3,
            textColor=colors.HexColor("#22313F"),
            leftIndent=0,
            rightIndent=0,
            spaceBefore=0,
            spaceAfter=0,
        ),
        dedent=0,
    )
    table = Table([[pre]], colWidths=[6.55 * inch])
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#F5F7FA")),
                ("BOX", (0, 0), (-1, -1), 0.5, colors.HexColor("#D6DDE5")),
                ("INNERPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]
        )
    )
    return table


def make_list(items: Iterable[str], styles: StyleSheet1, ordered: bool) -> ListFlowable:
    flow_items = []
    for item in items:
        para = Paragraph(inline_markup(item), styles["ListBody"])
        flow_items.append(ListItem(para))
    bullet_type = "1" if ordered else "bullet"
    return ListFlowable(
        flow_items,
        bulletType=bullet_type,
        leftIndent=16,
        bulletFontName="Times-Roman",
        bulletFontSize=10,
        bulletOffsetY=1,
        spaceBefore=0,
        spaceAfter=8,
    )


def render_blocks(
    blocks: list[Block],
    styles: StyleSheet1,
    start_index: int = 0,
    stop_before_heading_level: int | None = None,
) -> tuple[list, int]:
    story = []
    i = start_index
    first_h1_seen = start_index != 0

    while i < len(blocks):
        block = blocks[i]
        if block.kind == "heading" and stop_before_heading_level is not None:
            if block.level is not None and block.level <= stop_before_heading_level:
                break

        if block.kind == "heading":
            assert block.text is not None and block.level is not None
            if block.level == 1 and first_h1_seen:
                story.append(PageBreak())
            if block.level == 1:
                first_h1_seen = True

            if block.level == 3 and block.text in CALLOUT_STYLES:
                callout_story, next_index = render_blocks(
                    blocks,
                    styles,
                    start_index=i + 1,
                    stop_before_heading_level=3,
                )
                rows = [[Paragraph(inline_markup(block.text), styles["CalloutTitle"])]]
                for flowable in callout_story:
                    rows.append([flowable])
                box = Table(rows, colWidths=[6.55 * inch], splitByRow=1)
                box.setStyle(
                    TableStyle(
                        [
                            ("BACKGROUND", (0, 0), (-1, -1), CALLOUT_STYLES[block.text]),
                            ("BOX", (0, 0), (-1, -1), 0.6, colors.HexColor("#C7D4E2")),
                            ("INNERPADDING", (0, 0), (-1, -1), 9),
                            ("TOPPADDING", (0, 0), (-1, -1), 8),
                            ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
                        ]
                    )
                )
                story.append(box)
                story.append(Spacer(1, 8))
                i = next_index
                continue

            story.append(heading_para(block.text, block.level, styles))
            i += 1
            continue

        if block.kind == "paragraph":
            assert block.text is not None
            story.append(Paragraph(inline_markup(block.text), styles["ManualBody"]))
            i += 1
            continue

        if block.kind == "ulist":
            assert block.items is not None
            story.append(make_list(block.items, styles, ordered=False))
            i += 1
            continue

        if block.kind == "olist":
            assert block.items is not None
            story.append(make_list(block.items, styles, ordered=True))
            i += 1
            continue

        if block.kind == "code":
            story.append(code_block(block.text or ""))
            story.append(Spacer(1, 8))
            i += 1
            continue

        i += 1

    return story, i


def build_story(doc: ManualDocTemplate, markdown_text: str, styles: StyleSheet1) -> list:
    blocks = parse_markdown(markdown_text)
    body_story, _ = render_blocks(blocks, styles)

    story = [
        Spacer(1, 1.3 * inch),
        Paragraph(GUIDE_TITLE, styles["TitlePageTitle"]),
        Paragraph(GUIDE_SUBTITLE, styles["TitlePageSubtitle"]),
        Spacer(1, 0.15 * inch),
        Paragraph(
            "Markdown source rendered into a print-first PDF for study, maintenance, and review.",
            ParagraphStyle(
                "TitleNote",
                parent=styles["ManualBody"],
                alignment=TA_CENTER,
                textColor=colors.HexColor("#5D7186"),
            ),
        ),
        PageBreak(),
        Paragraph("Contents", styles["TOCHeading"]),
        doc.toc,
        PageBreak(),
    ]
    story.extend(body_story)
    return story


def render_pdf(input_md: Path, output_pdf: Path) -> None:
    styles = make_styles()
    doc = ManualDocTemplate(str(output_pdf), styles=styles)
    markdown_text = input_md.read_text(encoding="utf-8")
    story = build_story(doc, markdown_text, styles)
    doc.multiBuild(
        story,
        canvasmaker=lambda *args, **kwargs: NumberedCanvas(
            *args, section_map_ref=doc.heading_map, **kwargs
        ),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_md", type=Path, help="Markdown source file")
    parser.add_argument("output_pdf", type=Path, help="Output PDF path")
    args = parser.parse_args()
    render_pdf(args.input_md, args.output_pdf)


if __name__ == "__main__":
    main()
