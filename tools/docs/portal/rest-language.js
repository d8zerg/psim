(() => {
  const link = document.querySelector('[data-language-switch]');
  const destination = link.getAttribute('href');
  link.addEventListener('click', () => link.setAttribute('href', destination + location.hash));
  if (document.documentElement.lang !== 'ru') return;
  const labels = new Map([
    ['Request samples', 'Примеры запросов'], ['Response samples', 'Примеры ответов'],
    ['Responses', 'Ответы'], ['Request Body schema:', 'Схема тела запроса:'],
    ['Response Schema:', 'Схема ответа:'], ['Content type', 'Тип содержимого'],
    ['Content type:', 'Тип содержимого:'], ['Expand all', 'Развернуть всё'],
    ['Collapse all', 'Свернуть всё'], ['API docs by Redocly', 'Документация API - Redocly'],
    ['Download OpenAPI specification', 'Скачать спецификацию OpenAPI'],
    ['Search...', 'Поиск...'], ['No results found', 'Результаты не найдены'],
  ]);
  const translate = (root) => {
    if (root.nodeType !== Node.ELEMENT_NODE || root.closest('pre,code,script,style')) return;
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    while (walker.nextNode()) {
      const node = walker.currentNode;
      if (node.parentElement.closest('pre,code,script,style')) continue;
      const value = node.nodeValue.trim();
      if (labels.has(value)) node.nodeValue = node.nodeValue.replace(value, labels.get(value));
      else if (node.parentElement.closest('button') && value === 'Copy') node.nodeValue = 'Копировать';
      else if (node.parentElement.closest('a[download],button') && value === 'Download') node.nodeValue = 'Скачать';
    }
    for (const input of root.querySelectorAll('input[placeholder]')) {
      if (labels.has(input.placeholder)) input.placeholder = labels.get(input.placeholder);
    }
  };
  translate(document.body);
  new MutationObserver((changes) => {
    for (const change of changes) for (const node of change.addedNodes) translate(node);
  }).observe(document.body, { childList: true, subtree: true });
})();
