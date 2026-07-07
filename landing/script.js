document.addEventListener('DOMContentLoaded', () => {
    const downloadBtn = document.getElementById('download-btn');

    downloadBtn.addEventListener('click', () => {
        const originalText = downloadBtn.innerText;
        
        // Efeito rápido de interface
        downloadBtn.innerText = 'Iniciando download...';
        downloadBtn.style.opacity = '0.8';
        downloadBtn.style.transform = 'scale(0.98)';

        // Retorna o botão ao normal depois de 2.5 segundos
        setTimeout(() => {
            downloadBtn.innerText = originalText;
            downloadBtn.style.opacity = '1';
            downloadBtn.style.transform = 'scale(1)';
        }, 2500);
    });
});