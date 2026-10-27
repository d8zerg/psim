(() => {
  const link = document.querySelector('[data-language-switch]');
  const data = document.getElementById('document-language-anchors');
  if (!link || !data) return;
  const anchors = JSON.parse(data.textContent);
  const destination = link.getAttribute('href').split('#')[0];
  const update = () => {
    const headings = [...document.querySelectorAll('article h1[id],article h2[id],article h3[id],article h4[id],article h5[id],article h6[id]')];
    const visible = headings.filter((heading) => heading.getBoundingClientRect().top <= 140).at(-1);
    const current = decodeURIComponent(location.hash.slice(1)) || visible?.id;
    const target = anchors[current] || current;
    link.setAttribute('href', destination + (target ? '#' + encodeURIComponent(target) : ''));
  };
  link.addEventListener('click', update);
  window.addEventListener('hashchange', update);
  update();
})();
