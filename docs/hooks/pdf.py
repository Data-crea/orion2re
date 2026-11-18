from bs4 import BeautifulSoup


def on_page_content(html, **kwargs):
    soup = BeautifulSoup(html, "html.parser")
    # mkdocs-to-pdf removes blank paragraphs, including generated reference
    # anchors. Keep these anchors inside the following nonempty heading.
    for paragraph in soup.find_all("p"):
        anchors = paragraph.find_all("a", id=True, href=False)
        heading = paragraph.find_next_sibling()
        if (anchors and not paragraph.get_text(strip=True)
                and heading is not None and heading.name in ("h1", "h2", "h3", "h4", "h5", "h6")
                and len(paragraph.find_all()) == len(anchors)):
            for anchor in reversed(anchors):
                heading.insert(0, anchor.extract())
            paragraph.decompose()
    return str(soup)
